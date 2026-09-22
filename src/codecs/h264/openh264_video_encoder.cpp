#include "codecs/h264/openh264_video_encoder.h"
#include "common/latency_tracer.h"
#include "common/logging.h"

#include <algorithm>

#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <system_wrappers/include/clock.h>

namespace {

const int kKeyFrameIntervalFrames = 3000;
const int kLowH264QpThreshold = 24;
const int kHighH264QpThreshold = 37;

int NumberOfThreads(std::optional<int> encoder_thread_limit, int number_of_cores) {
    const int cores = std::max(number_of_cores, 1);
    return std::clamp(encoder_thread_limit.value_or(cores), 1, cores);
}

} // namespace

std::unique_ptr<webrtc::VideoEncoder> Openh264VideoEncoder::Create(Args args) {
    return std::make_unique<Openh264VideoEncoder>(args);
}

Openh264VideoEncoder::Openh264VideoEncoder(Args args)
    : width_(0),
      height_(0),
      fps_adjuster_(args.fps),
      target_bitrate_bps_(0),
      number_of_cores_(1),
      bitrate_adjuster_(webrtc::Clock::GetRealTimeClock(), .85, 1),
      callback_(nullptr) {}

int32_t Openh264VideoEncoder::InitEncode(const webrtc::VideoCodec *codec_settings,
                                         const VideoEncoder::Settings &settings) {
    if (codec_settings->codecType != webrtc::kVideoCodecH264) {
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    codec_ = *codec_settings;
    width_ = codec_settings->width;
    height_ = codec_settings->height;
    if (codec_settings->maxFramerate > 0) {
        fps_adjuster_ = codec_settings->maxFramerate;
    }
    target_bitrate_bps_ = codec_settings->startBitrate * 1000;
    number_of_cores_ = settings.number_of_cores;
    encoder_thread_limit_ = settings.encoder_thread_limit;
    bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);

    encoded_image_.timing_.flags = webrtc::VideoSendTiming::TimingFrameFlags::kInvalid;
    encoded_image_.content_type_ = webrtc::VideoContentType::UNSPECIFIED;

    encoder_.reset();

    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t
Openh264VideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) {
    callback_.store(callback, std::memory_order_release);
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t Openh264VideoEncoder::Release() {
    callback_.store(nullptr, std::memory_order_release);
    encoder_.reset();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t Openh264VideoEncoder::Encode(const webrtc::VideoFrame &frame,
                                     const std::vector<webrtc::VideoFrameType> *frame_types) {
    if (!frame_types) {
        return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
    }

    if ((*frame_types)[0] == webrtc::VideoFrameType::kEmptyFrame) {
        return WEBRTC_VIDEO_CODEC_OK;
    }

    auto i420_buffer = frame.video_frame_buffer()->ToI420();
    if (!i420_buffer) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    if (encoder_ && (width_ != i420_buffer->width() || height_ != i420_buffer->height())) {
        encoder_.reset();
    }

    if (!encoder_) {
        width_ = i420_buffer->width();
        height_ = i420_buffer->height();

        EncoderConfig config;
        config.width = width_;
        config.height = height_;
        config.fps = fps_adjuster_;
        config.bitrate = target_bitrate_bps_;
        config.keyframe_interval = kKeyFrameIntervalFrames;
        config.idr_interval = kKeyFrameIntervalFrames;
        config.rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
        config.frame_dropping = codec_.GetFrameDropEnabled();
        config.thread_count = NumberOfThreads(encoder_thread_limit_, number_of_cores_);
        encoder_ = Openh264Encoder::Create(config);

        if (!encoder_) {
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
    }

    if ((*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey) {
        encoder_->ForceIntraFrame();
    }

    bool encoded =
        encoder_->Encode(i420_buffer, [this, &frame](uint8_t *buffer, int size, bool is_keyframe) {
            SendFrame(frame, buffer, size, is_keyframe);
        });

    if (!encoded) {
        auto cb = callback_.load(std::memory_order_acquire);
        if (cb) {
            cb->OnDroppedFrame(webrtc::EncodedImageCallback::DropReason::kDroppedByEncoder);
        }
    }

    return WEBRTC_VIDEO_CODEC_OK;
}

void Openh264VideoEncoder::SetRates(const RateControlParameters &parameters) {
    if (parameters.bitrate.get_sum_bps() <= 0 || parameters.framerate_fps <= 0) {
        return;
    }
    target_bitrate_bps_ = parameters.bitrate.get_sum_bps();
    fps_adjuster_ = parameters.framerate_fps;
    bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);

    if (latency::Enabled()) {
        latency::SetBitrateKbps(
            target_bitrate_bps_ / 1000, target_bitrate_bps_ / 1000,
            static_cast<int>(bitrate_adjuster_.GetEstimatedBitrateBps().value_or(0) / 1000));
    }

    if (!encoder_) {
        return;
    }
    encoder_->SetRates(target_bitrate_bps_, fps_adjuster_);
}

webrtc::VideoEncoder::EncoderInfo Openh264VideoEncoder::GetEncoderInfo() const {
    EncoderInfo info;
    info.supports_native_handle = false;
    info.is_hardware_accelerated = false;
    info.has_trusted_rate_controller = false;
    info.implementation_name = "OpenH264";
    info.scaling_settings =
        VideoEncoder::ScalingSettings(kLowH264QpThreshold, kHighH264QpThreshold);
    return info;
}

void Openh264VideoEncoder::SendFrame(const webrtc::VideoFrame &frame, uint8_t *buffer, int size,
                                     bool is_keyframe) {
    bitrate_adjuster_.Update(size);

    auto encoded_image_buffer = webrtc::EncodedImageBuffer::Create(buffer, size);

    webrtc::CodecSpecificInfo codec_specific;
    codec_specific.codecType = webrtc::kVideoCodecH264;
    codec_specific.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;

    encoded_image_.SetEncodedData(encoded_image_buffer);
    encoded_image_.SetRtpTimestamp(frame.rtp_timestamp());
    encoded_image_.SetColorSpace(frame.color_space());
    encoded_image_._encodedWidth = width_;
    encoded_image_._encodedHeight = height_;
    encoded_image_.capture_time_ms_ = frame.render_time_ms();
    encoded_image_.ntp_time_ms_ = frame.ntp_time_ms();
    encoded_image_.rotation_ = frame.rotation();
    encoded_image_._frameType = is_keyframe ? webrtc::VideoFrameType::kVideoFrameKey
                                            : webrtc::VideoFrameType::kVideoFrameDelta;

    bitstream_parser_.ParseBitstream(*encoded_image_buffer);
    encoded_image_.qp_ = bitstream_parser_.GetLastSliceQp().value_or(-1);

    auto cb = callback_.load(std::memory_order_acquire);
    if (!cb) {
        return;
    }

    auto result = cb->OnEncodedImage(encoded_image_, &codec_specific);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
        DEBUG_PRINT("Failed to send the frame => %d", result.error);
    }
}
