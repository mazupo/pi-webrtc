#include "recorder/video_recorder.h"

#include "codecs/encoder_factory.h"
#include "common/latency_tracer.h"
#include "common/logging.h"

namespace {

constexpr size_t kFrameQueueDepth = 2;
// Bits per pixel of the VBR target bitrate.
constexpr double kH264BitsPerPixel = 0.1;
constexpr double kAv1BitsPerPixel = 0.06;

} // namespace

std::unique_ptr<VideoRecorder> VideoRecorder::Create(int width, int height, int fps,
                                                     bool hw_accel) {
    bool use_hw = hw_accel && IsHwEncoderAvailable();
    if (hw_accel && !use_hw) {
        WARN_PRINT("No hardware encoder found; recording with OpenH264.");
    }
    return std::make_unique<VideoRecorder>(width, height, fps, use_hw);
}

VideoRecorder::VideoRecorder(int width, int height, int fps, bool use_hw)
    : Recorder(),
      fps(fps),
      width(width),
      height(height),
      use_hw_(use_hw),
      format_(VideoEncoderFormat(use_hw)),
      frame_buffer_queue(kFrameQueueDepth),
      base_time_initialized(false) {}

void VideoRecorder::InitializeEncoderCtx(AVCodecContext *&encoder) {
    AVRational frame_rate = {.num = (int)fps, .den = 1};

    const AVCodec *codec =
        avcodec_find_encoder(format_ == V4L2_PIX_FMT_H264 ? AV_CODEC_ID_H264 : AV_CODEC_ID_AV1);
    encoder = avcodec_alloc_context3(codec);
    encoder->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder->width = width;
    encoder->height = height;
    encoder->framerate = frame_rate;
    encoder->time_base = av_inv_q(frame_rate);
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

void VideoRecorder::OnBuffer(V4L2FrameBufferRef frame_buffer) {
    if (frame_buffer_queue.full()) {
        if (latency::Enabled()) {
            latency::Count(latency::Counter::kRecorderQueueFull);
        }
        return;
    }

    // A frame in CPU memory is copied, because the driver takes it back when this callback
    // returns. One without lives in NVMM and is passed on by reference.
    frame_buffer_queue.push(frame_buffer->Data() ? frame_buffer->Clone() : frame_buffer);
}

void VideoRecorder::OnStart() {
    {
        std::lock_guard<std::mutex> lock(encoder_mtx_);
        base_time_initialized = false;
        encoder_.reset();
    }
    frame_buffer_queue.clear();
}

void VideoRecorder::OnEncoded(uint8_t *start, uint32_t length, timeval timestamp, uint32_t flags) {
    if (!st) {
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (av_new_packet(pkt, length) < 0) {
        av_packet_free(&pkt);
        return;
    }
    memcpy(pkt->data, start, length);

    pkt->stream_index = st->index;
    if (flags & V4L2_BUF_FLAG_KEYFRAME) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    if (!base_time_initialized) {
        base_time_ = timestamp;
        base_time_initialized = true;
    }

    int64_t elapsed_usec = (int64_t)(timestamp.tv_sec - base_time_.tv_sec) * 1000000LL +
                           (int64_t)(timestamp.tv_usec - base_time_.tv_usec);

    AVRational usec_base = {1, 1000000};

    pkt->pts = pkt->dts = av_rescale_q(elapsed_usec, usec_base, st->time_base);

    OnPacketed(pkt);

    av_packet_free(&pkt);
}

void VideoRecorder::Encode(V4L2FrameBufferRef frame_buffer) {
    if (!encoder_) {
        double bits_per_pixel = format_ == V4L2_PIX_FMT_H264 ? kH264BitsPerPixel : kAv1BitsPerPixel;
        EncoderConfig config = {
            .width = width,
            .height = height,
            .fps = fps,
            .bitrate = static_cast<int>(width * height * fps * bits_per_pixel),
            .keyframe_interval = fps,
            .idr_interval = fps,
            .is_dma_src = frame_buffer->GetDmaFd() > 0,
            .src_pix_fmt = frame_buffer->format(),
            .src_plane_height = frame_buffer->plane_height(),
            .dst_pix_fmt = format_,
            .rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
        };
        encoder_ = CreateVideoEncoder(config, use_hw_);
        if (!encoder_) {
            return;
        }
    }

    encoder_->EmplaceBuffer(frame_buffer, [this, frame_buffer](V4L2FrameBufferRef encoded_buffer) {
        OnEncoded((uint8_t *)encoded_buffer->Data(), encoded_buffer->size(),
                  frame_buffer->timestamp(), encoded_buffer->flags());
    });
}

bool VideoRecorder::ConsumeBuffer() {
    auto item = frame_buffer_queue.pop(10);

    if (!item) {
        return false;
    }

    auto frame_buffer = item.value();

    std::lock_guard<std::mutex> lock(encoder_mtx_);

    if (!IsEncoderReady()) {
        return false;
    }

    Encode(frame_buffer);

    return true;
}

bool VideoRecorder::IsEncoderReady() { return encoder != nullptr; }
