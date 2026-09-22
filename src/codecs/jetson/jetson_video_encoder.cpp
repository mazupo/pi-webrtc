#include "codecs/jetson/jetson_video_encoder.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_frame_buffer.h"

#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <system_wrappers/include/clock.h>

const int kKeyFrameIntervalFrames = 3000;

std::unique_ptr<webrtc::VideoEncoder> JetsonVideoEncoder::Create(Args args) {
    return std::make_unique<JetsonVideoEncoder>(args);
}

JetsonVideoEncoder::JetsonVideoEncoder(Args args)
    : fps_adjuster_(args.fps),
      bitrate_adjuster_(webrtc::Clock::GetRealTimeClock(), .85, 1),
      callback_(nullptr) {
    if (args.max_playout_delay_ms >= 0) {
        playout_delay_ =
            webrtc::VideoPlayoutDelay(webrtc::TimeDelta::Millis(args.min_playout_delay_ms),
                                      webrtc::TimeDelta::Millis(args.max_playout_delay_ms));
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
    bitrate_adjuster_.SetTargetBitrateBps(codec_settings->startBitrate * 1000);

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
    auto v4l2_frame_buffer = V4L2FrameBufferRef(static_cast<V4L2FrameBuffer *>(frame_buffer.get()));

    if (!encoder_) {
        auto codec_fmt = GetV4L2CodecFormat(codec_.codecType);
        if (codec_fmt == 0) {
            return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
        }
        EncoderConfig config;
        config.width = width_;
        config.height = height_;
        config.dst_pix_fmt = codec_fmt;
        config.is_dma_src = frame_buffer->type() == webrtc::VideoFrameBuffer::Type::kNative;
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
    bitrate_adjuster_.SetTargetBitrateBps(parameters.bitrate.get_sum_bps());
    fps_adjuster_ = parameters.framerate_fps;

    if (latency::Enabled()) {
        latency::SetBitrateKbps(
            parameters.bitrate.get_sum_bps() / 1000,
            bitrate_adjuster_.GetAdjustedBitrateBps() / 1000,
            static_cast<int>(bitrate_adjuster_.GetEstimatedBitrateBps().value_or(0) / 1000));
    }

    if (!encoder_) {
        return;
    }
    encoder_->SetFps(fps_adjuster_);
    encoder_->SetBitrate(bitrate_adjuster_.GetAdjustedBitrateBps());
}

webrtc::VideoEncoder::EncoderInfo JetsonVideoEncoder::GetEncoderInfo() const {
    EncoderInfo info;
    info.supports_native_handle = true;
    info.is_hardware_accelerated = true;
    info.has_trusted_rate_controller = true;
    info.implementation_name = "Jetson Hardware Encoder";
    return info;
}

void JetsonVideoEncoder::SendFrame(const webrtc::VideoFrame &frame, V4L2Buffer &encoded_buffer) {
    bitrate_adjuster_.Update(encoded_buffer.length);

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
