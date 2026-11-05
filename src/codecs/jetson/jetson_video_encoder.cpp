#include "codecs/jetson/jetson_video_encoder.h"
#include "common/logging.h"
#include "common/v4l2_frame_buffer.h"

#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>

#include <algorithm>
#include <span>

const int kKeyFrameIntervalFrames = 3000;

namespace {

// Resolution gears in kbps at 60 fps: min_start, min, max (0 = no cap), from the highest usable
// bitrate seen across lighting. NVENC starves below 1.2x that, so WebRTC steps down early.
// The first gear is the lowest: below 270p a still scene can't reach the 0.95x max to shift up.
struct Gear {
    int width, height, min_start_kbps, min_kbps, max_kbps;
};
constexpr int kGearFps = 60;
constexpr Gear kH264Gears[] = {
    {480, 270, 0, 251, 750},       {640, 360, 750, 570, 1350},  {960, 540, 1350, 1026, 1350},
    {1280, 720, 1350, 1026, 2400}, {1920, 1080, 2400, 1824, 0},
};
// AV1 540p needs more than 720p in bright light, so it only passes through.
constexpr Gear kAv1Gears[] = {
    {480, 270, 0, 342, 750},       {640, 360, 750, 570, 1350},  {960, 540, 1350, 1026, 1350},
    {1280, 720, 1350, 1026, 4500}, {1920, 1080, 4500, 3420, 0},
};
// WebRTC shifts up only once the encoded bitrate passes 0.95x the gear's max, which also caps the
// target, so NVENC's undershoot may stall it; spend up to 25% more while the network has room.
constexpr int64_t kGearHeadroomPercent = 125;

std::vector<webrtc::VideoEncoder::ResolutionBitrateLimits> ScaleGears(std::span<const Gear> gears,
                                                                      int fps) {
    const double scale = 1000.0 * fps / kGearFps;
    std::vector<webrtc::VideoEncoder::ResolutionBitrateLimits> limits;
    for (const auto &g : gears) {
        // 100 Mbps stands in for "no cap"; WebRTC interpolates these as floats.
        const int max_bps = g.max_kbps ? static_cast<int>(g.max_kbps * scale) : 100000000;
        limits.emplace_back(g.width * g.height, static_cast<int>(g.min_start_kbps * scale),
                            std::max(static_cast<int>(g.min_kbps * scale), 30000), max_bps);
    }
    return limits;
}

} // namespace

std::unique_ptr<webrtc::VideoEncoder>
JetsonVideoEncoder::Create(RtcEncoderConfig config, webrtc::VideoCodecType codec_type) {
    return std::make_unique<JetsonVideoEncoder>(config, codec_type);
}

JetsonVideoEncoder::JetsonVideoEncoder(RtcEncoderConfig config, webrtc::VideoCodecType codec_type)
    : target_fps_(config.fps),
      target_bitrate_bps_(0),
      max_bitrate_kbps_(config.max_bitrate),
      callback_(nullptr) {
    if (config.max_playout_delay_ms >= 0) {
        playout_delay_ =
            webrtc::VideoPlayoutDelay(webrtc::TimeDelta::Millis(config.min_playout_delay_ms),
                                      webrtc::TimeDelta::Millis(config.max_playout_delay_ms));
    }
    if (!config.no_adaptive && codec_type == webrtc::kVideoCodecH264) {
        bitrate_limits_ = ScaleGears(kH264Gears, config.fps);
    } else if (!config.no_adaptive && codec_type == webrtc::kVideoCodecAV1) {
        bitrate_limits_ = ScaleGears(kAv1Gears, config.fps);
    }
}

int32_t JetsonVideoEncoder::InitEncode(const webrtc::VideoCodec *codec_settings,
                                       const VideoEncoder::Settings &settings) {
    auto scalability_mode = codec_settings->GetScalabilityMode();
    if (scalability_mode && *scalability_mode != webrtc::ScalabilityMode::kL1T1) {
        auto name = webrtc::ScalabilityModeToString(*scalability_mode);
        ERROR_PRINT("Unsupported scalability mode: %.*s", static_cast<int>(name.size()),
                    name.data());
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    codec_ = *codec_settings;
    width_ = codec_settings->width;
    height_ = codec_settings->height;
    target_bitrate_bps_ = codec_settings->startBitrate * 1000;

    encoded_image_.timing_.flags = webrtc::VideoSendTiming::TimingFrameFlags::kInvalid;
    encoded_image_.content_type_ = webrtc::VideoContentType::UNSPECIFIED;

    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t JetsonVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) {
    callback_.store(callback, std::memory_order_release);
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t JetsonVideoEncoder::Release() {
    callback_.store(nullptr, std::memory_order_release);
    encoder_.reset();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t JetsonVideoEncoder::Encode(const webrtc::VideoFrame &frame,
                                   const std::vector<webrtc::VideoFrameType> *frame_types) {
    if (!frame_types) {
        return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
    }

    if ((*frame_types)[0] == webrtc::VideoFrameType::kEmptyFrame) {
        return WEBRTC_VIDEO_CODEC_OK;
    }
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer = frame.video_frame_buffer();

    if (frame_buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    auto v4l2_frame_buffer = V4L2FrameBufferRef(static_cast<V4L2FrameBuffer *>(frame_buffer.get()));

    if (!encoder_) {
        auto codec_fmt = GetV4L2CodecFormat(codec_.codecType);
        if (codec_fmt == 0) {
            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
        }
        EncoderConfig config;
        config.width = width_;
        config.height = height_;
        config.fps = target_fps_;
        config.bitrate = target_bitrate_bps_;
        config.max_bitrate = codec_.maxBitrate * 1000;
        config.dst_pix_fmt = codec_fmt;
        config.is_dma_src = v4l2_frame_buffer->GetDmaFd() > 0;
        config.keyframe_interval = kKeyFrameIntervalFrames;
        config.idr_interval = kKeyFrameIntervalFrames;
        encoder_ = JetsonEncoder::Create(config);
    }

    if ((*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey) {
        encoder_->ForceKeyFrame();
    }

    encoder_->EmplaceBuffer(v4l2_frame_buffer, [this, frame](V4L2FrameBufferRef encoded_buffer) {
        auto v4l2buffer = encoded_buffer->GetRawBuffer();
        SendFrame(frame, v4l2buffer);
    });

    return WEBRTC_VIDEO_CODEC_OK;
}

void JetsonVideoEncoder::SetRates(const RateControlParameters &parameters) {
    if (parameters.bitrate.get_sum_bps() <= 0 || parameters.framerate_fps <= 0) {
        return;
    }
    target_bitrate_bps_ = parameters.bitrate.get_sum_bps();
    target_fps_ = parameters.framerate_fps;
    const int gear_max_kbps = static_cast<int>(codec_.maxBitrate);
    if (!bitrate_limits_.empty() && gear_max_kbps < max_bitrate_kbps_ &&
        target_bitrate_bps_ >= gear_max_kbps * 1000) {
        const int64_t room = std::min<int64_t>(parameters.bandwidth_allocation.bps(),
                                               int64_t{max_bitrate_kbps_} * 1000);
        target_bitrate_bps_ =
            static_cast<int>(std::clamp(room, int64_t{target_bitrate_bps_},
                                        int64_t{target_bitrate_bps_} * kGearHeadroomPercent / 100));
    }

    if (!encoder_) {
        return;
    }
    encoder_->SetFps(target_fps_);
    encoder_->SetBitrate(target_bitrate_bps_);
}

webrtc::VideoEncoder::EncoderInfo JetsonVideoEncoder::GetEncoderInfo() const {
    EncoderInfo info;
    info.supports_native_handle = true;
    info.is_hardware_accelerated = true;
    info.has_trusted_rate_controller = true;
    info.implementation_name = "Jetson Hardware Encoder";
    if (!bitrate_limits_.empty()) {
        info.is_qp_trusted = false;
        info.resolution_bitrate_limits = bitrate_limits_;
        info.scaling_settings.min_pixels_per_frame = bitrate_limits_.front().frame_size_pixels;
    }
    return info;
}

void JetsonVideoEncoder::SendFrame(const webrtc::VideoFrame &frame, V4L2Buffer &encoded_buffer) {
    auto encoded_image_buffer =
        webrtc::EncodedImageBuffer::Create((uint8_t *)encoded_buffer.start, encoded_buffer.length);

    webrtc::CodecSpecificInfo codec_specific;
    codec_specific.codecType = codec_.codecType;

    if (codec_specific.codecType == webrtc::kVideoCodecH264) {
        codec_specific.codecSpecific.H264.packetization_mode =
            webrtc::H264PacketizationMode::NonInterleaved;
    }

    encoded_image_.SetEncodedData(encoded_image_buffer);
    encoded_image_.SetRtpTimestamp(frame.rtp_timestamp());
    encoded_image_.SetColorSpace(frame.color_space());
    encoded_image_._encodedWidth = width_;
    encoded_image_._encodedHeight = height_;
    encoded_image_.capture_time_ms_ = frame.render_time_ms();
    encoded_image_.ntp_time_ms_ = frame.ntp_time_ms();
    encoded_image_.rotation_ = frame.rotation();
    encoded_image_.SetPlayoutDelay(playout_delay_);
    encoded_image_._frameType = encoded_buffer.flags & V4L2_BUF_FLAG_KEYFRAME
                                    ? webrtc::VideoFrameType::kVideoFrameKey
                                    : webrtc::VideoFrameType::kVideoFrameDelta;

    auto cb = callback_.load(std::memory_order_acquire);
    if (!cb) {
        return;
    }
    auto result = cb->OnEncodedImage(encoded_image_, &codec_specific);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
        DEBUG_PRINT("Failed to send the frame => %d", result.error);
    }
}

uint32_t JetsonVideoEncoder::GetV4L2CodecFormat(webrtc::VideoCodecType type) {
    switch (type) {
        case webrtc::kVideoCodecVP8:
            return V4L2_PIX_FMT_VP8;
        case webrtc::kVideoCodecVP9:
            return V4L2_PIX_FMT_VP9;
        case webrtc::kVideoCodecAV1:
            return V4L2_PIX_FMT_AV1;
        case webrtc::kVideoCodecH264:
            return V4L2_PIX_FMT_H264;
        default:
            return 0;
    }
}
