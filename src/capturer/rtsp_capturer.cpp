#include "capturer/rtsp_capturer.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>

extern "C" {
#include <libavutil/opt.h>
}

#include "capturer/decoder_factory.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_utils.h"

namespace {

constexpr int64_t kSocketTimeoutUs = 5'000'000;
constexpr auto kRetryInterval = std::chrono::seconds(1);

std::string ErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

uint32_t ToFourcc(AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264:
            return V4L2_PIX_FMT_H264;
        case AV_CODEC_ID_HEVC:
            return V4L2_PIX_FMT_HEVC;
        case AV_CODEC_ID_MJPEG:
            return V4L2_PIX_FMT_MJPEG;
        default:
            return 0;
    }
}

bool IsAnnexB(const uint8_t *data, int size) {
    return (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) ||
           (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1);
}

timeval MonotonicNow() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return {ts.tv_sec, ts.tv_nsec / 1000};
}

} // namespace

std::shared_ptr<RtspCapturer> RtspCapturer::Create(Args args) {
    auto ptr = std::make_shared<RtspCapturer>(args);
    ptr->Initialize();
    ptr->StartCapture();
    return ptr;
}

RtspCapturer::RtspCapturer(Args args)
    : config_(args),
      url_(args.camera),
      hw_decoder_(false),
      has_first_keyframe_(false),
      retrying_(false),
      stream_index_(-1),
      fmt_ctx_(nullptr),
      packet_(av_packet_alloc()),
      stopping_(false) {}

RtspCapturer::~RtspCapturer() {
    stopping_ = true;
    retry_cv_.notify_all();
    worker_.reset();
    decoder_.reset();
    Close();
    av_packet_free(&packet_);
}

void RtspCapturer::Initialize() {
    INFO_PRINT("Connecting to RTSP camera %s", url_.c_str());
    // The track source sizes itself at creation, so wait until the stream reports its size.
    while (!Open(&stream_)) {
        WaitToRetry();
    }
    if (stream_.format == 0) {
        ERROR_PRINT("The RTSP stream uses a codec other than H264, H265 or MJPEG.");
        exit(EXIT_FAILURE);
    }
    INFO_PRINT("RTSP stream: %s %dx%d@%d; --width, --height and --fps do not apply to RTSP.",
               v4l2_util::FourccToString(stream_.format).c_str(), stream_.width, stream_.height,
               stream_.fps);
    CreateDecoder();
}

bool RtspCapturer::Open(StreamInfo *info) {
    // Logs only the first failure while retrying; the next success logs the recovery.
    auto fail = [this](const std::string &reason) {
        if (!retrying_ && !stopping_) {
            WARN_PRINT("Could not open %s: %s; retrying every second.", url_.c_str(),
                       reason.c_str());
        }
        retrying_ = true;
        Close();
        return false;
    };

    fmt_ctx_ = avformat_alloc_context();
    if (!fmt_ctx_) {
        return fail("out of memory");
    }
    fmt_ctx_->interrupt_callback.callback = &RtspCapturer::Interrupt;
    fmt_ctx_->interrupt_callback.opaque = this;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "rtsp_flags", "prefer_tcp", 0);
    av_dict_set(&options, "allowed_media_types", "video", 0);
    av_dict_set(&options, "fflags", "nobuffer", 0);
    // FFmpeg 4.4 calls the socket timeout `stimeout`; its `timeout` would switch to listen mode.
    const AVInputFormat *rtsp = av_find_input_format("rtsp");
    bool has_stimeout = rtsp && av_opt_find((void *)&rtsp->priv_class, "stimeout", nullptr, 0,
                                            AV_OPT_SEARCH_FAKE_OBJ);
    av_dict_set_int(&options, has_stimeout ? "stimeout" : "timeout", kSocketTimeoutUs, 0);

    int ret = avformat_open_input(&fmt_ctx_, url_.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret < 0) {
        return fail(ErrorString(ret));
    }

    if ((ret = avformat_find_stream_info(fmt_ctx_, nullptr)) < 0 ||
        (ret = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)) < 0) {
        return fail("no video stream (" + ErrorString(ret) + ")");
    }
    stream_index_ = ret;

    AVStream *stream = fmt_ctx_->streams[stream_index_];
    AVCodecParameters *codecpar = stream->codecpar;
    if (codecpar->width <= 0 || codecpar->height <= 0) {
        return fail("the stream did not report its resolution");
    }

    info->format = ToFourcc(codecpar->codec_id);
    info->width = codecpar->width;
    info->height = codecpar->height;
    AVRational rate = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
    double fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0;
    // Streams without VUI timing often report the 90 kHz RTP clock instead.
    info->fps = fps >= 1 && fps <= 120 ? static_cast<int>(std::lround(fps)) : config_.fps;
    info->parameter_sets.clear();
    if (IsAnnexB(codecpar->extradata, codecpar->extradata_size)) {
        info->parameter_sets.assign(codecpar->extradata,
                                    codecpar->extradata + codecpar->extradata_size);
    }
    retrying_ = false;
    return true;
}

void RtspCapturer::Close() {
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
    }
    has_first_keyframe_ = false;
}

void RtspCapturer::CreateDecoder() {
    auto decoder =
        CreateVideoDecoder({stream_.width, stream_.height, stream_.format, config_.hw_accel});
    decoder_ = std::move(decoder.processor);
    hw_decoder_ = decoder.is_hardware;
    if (!decoder_ && stream_.format != V4L2_PIX_FMT_MJPEG) {
        ERROR_PRINT("Unable to create a decoder for the %s RTSP stream.",
                    v4l2_util::FourccToString(stream_.format).c_str());
        exit(EXIT_FAILURE);
    }
}

void RtspCapturer::Reconnect() {
    WaitToRetry();
    StreamInfo info;
    if (stopping_ || !Open(&info)) {
        return;
    }
    // Downstream was built for the first stream's size and codec.
    if (info.format != stream_.format || info.width != stream_.width ||
        info.height != stream_.height) {
        ERROR_PRINT("The RTSP stream changed from %s %dx%d to %s %dx%d; restart to follow it.",
                    v4l2_util::FourccToString(stream_.format).c_str(), stream_.width,
                    stream_.height, v4l2_util::FourccToString(info.format).c_str(), info.width,
                    info.height);
        exit(EXIT_FAILURE);
    }
    // The decoder stays: the first packet is a keyframe carrying the parameter sets.
    stream_.parameter_sets = std::move(info.parameter_sets);
    INFO_PRINT("Reconnected to %s", url_.c_str());
}

void RtspCapturer::WaitToRetry() {
    std::unique_lock<std::mutex> lock(retry_mtx_);
    retry_cv_.wait_for(lock, kRetryInterval, [this]() {
        return stopping_.load();
    });
}

int RtspCapturer::Interrupt(void *opaque) {
    return static_cast<RtspCapturer *>(opaque)->stopping_.load() ? 1 : 0;
}

void RtspCapturer::ReadPacket() {
    if (stopping_) {
        return;
    }
    if (!fmt_ctx_) {
        Reconnect();
        return;
    }

    int ret = av_read_frame(fmt_ctx_, packet_);
    if (ret < 0) {
        if (!stopping_) {
            WARN_PRINT("Lost the RTSP stream (%s); reconnecting.", ErrorString(ret).c_str());
        }
        Close();
        return;
    }
    if (packet_->stream_index != stream_index_) {
        av_packet_unref(packet_);
        return;
    }

    bool keyframe = packet_->flags & AV_PKT_FLAG_KEY;
    if (!has_first_keyframe_ && !keyframe) {
        av_packet_unref(packet_);
        return;
    }

    timeval timestamp = MonotonicNow();
    if (latency::Enabled()) {
        latency::RecordCapture(latency::SensorUs(timestamp), latency::NowUs());
    }

    uint32_t flags = keyframe ? V4L2_BUF_FLAG_KEYFRAME : 0;
    const auto &parameter_sets = stream_.parameter_sets;
    bool prepend = !has_first_keyframe_ && !parameter_sets.empty();
    has_first_keyframe_ = true;

    V4L2FrameBufferRef frame_buffer;
    if (decoder_ && !prepend) {
        // Decoders copy the packet inside EmplaceBuffer(), so no copy is needed here.
        frame_buffer = V4L2FrameBuffer::Create(
            stream_.width, stream_.height,
            V4L2Buffer(packet_->data, stream_.format, packet_->size, -1, flags, timestamp));
    } else {
        // Copied to prepend the parameter sets, or for MJPEG, which the encoder thread decodes
        // after the packet is freed.
        size_t prefix = prepend ? parameter_sets.size() : 0;
        frame_buffer = V4L2FrameBuffer::Create(stream_.width, stream_.height,
                                               prefix + packet_->size, stream_.format);
        if (prefix > 0) {
            memcpy(frame_buffer->MutableData(), parameter_sets.data(), prefix);
        }
        memcpy(frame_buffer->MutableData() + prefix, packet_->data, packet_->size);
        frame_buffer->SetTimestamp(timestamp);
    }

    OnFrame(frame_buffer, timestamp);
    av_packet_unref(packet_);
}

void RtspCapturer::OnFrame(V4L2FrameBufferRef frame_buffer, timeval timestamp) {
    if (!decoder_) {
        SetFrameBuffer(frame_buffer);
        stream_subject_.Next(frame_buffer);
        return;
    }
    decoder_->EmplaceBuffer(frame_buffer, [this, timestamp](V4L2FrameBufferRef decoded_buffer) {
        decoded_buffer->SetTimestamp(timestamp);
        SetFrameBuffer(decoded_buffer);
        stream_subject_.Next(decoded_buffer);
    });
}

int RtspCapturer::fps() const { return stream_.fps; }

int RtspCapturer::width(int stream_idx) const { return stream_.width; }

int RtspCapturer::height(int stream_idx) const { return stream_.height; }

bool RtspCapturer::is_dma_capture() const { return hw_decoder_; }

uint32_t RtspCapturer::format() const { return stream_.format; }

Args RtspCapturer::config() const { return config_; }

void RtspCapturer::SetFrameBuffer(V4L2FrameBufferRef frame_buffer) {
    std::lock_guard<std::mutex> lock(frame_mtx_);
    frame_buffer_ = std::move(frame_buffer);
}

webrtc::scoped_refptr<webrtc::I420BufferInterface> RtspCapturer::GetI420Frame(int stream_idx) {
    V4L2FrameBufferRef frame_buffer;
    {
        std::lock_guard<std::mutex> lock(frame_mtx_);
        frame_buffer = frame_buffer_;
    }
    if (!frame_buffer) {
        auto blank = webrtc::I420Buffer::Create(stream_.width, stream_.height);
        webrtc::I420Buffer::SetBlack(blank.get());
        return blank;
    }
    return frame_buffer->ToI420();
}

Subscription RtspCapturer::Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                     int stream_idx) {
    return stream_subject_.Subscribe(std::move(callback));
}

void RtspCapturer::StartCapture() {
    worker_ = std::make_unique<Worker>("RTSP Capturer", [this]() {
        ReadPacket();
    });
    worker_->Run();
}
