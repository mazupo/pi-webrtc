#include "rtc/custom_video_encoder_factory.h"

#include "common/latency_tracer.h"
#include "rtc/tracing_video_encoder.h"

#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_h264_encoder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_video_encoder.h"
#endif

#include <absl/strings/match.h>
#include <media/base/media_constants.h>
#include <modules/video_coding/codecs/av1/av1_svc_config.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

std::unique_ptr<webrtc::VideoEncoderFactory> CreateCustomVideoEncoderFactory(const Args &args) {
    return std::make_unique<CustomVideoEncoderFactory>(args);
}

std::vector<webrtc::SdpVideoFormat> CustomVideoEncoderFactory::GetSupportedFormats() const {
    std::vector<webrtc::SdpVideoFormat> supported_codecs;

    if (args_.hw_accel) {
#if defined(USE_RPI_HW_ENCODER)
        // hw h264
        supported_codecs.push_back(CreateH264Format(
            webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel4, "1"));
        supported_codecs.push_back(CreateH264Format(
            webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel4, "0"));
        supported_codecs.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                                    webrtc::H264Level::kLevel4, "1"));
        supported_codecs.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                                    webrtc::H264Level::kLevel4, "0"));
#elif defined(USE_JETSON_HW_ENCODER)
        // hw h264
        // It's tricky that react-native-webrtc not supports level 5.0 or higher.
        supported_codecs.push_back(CreateH264Format(
            webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel4, "1"));
        supported_codecs.push_back(CreateH264Format(
            webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel4, "0"));
        supported_codecs.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                                    webrtc::H264Level::kLevel4, "1"));
        supported_codecs.push_back(CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                                                    webrtc::H264Level::kLevel4, "0"));
        // av1
        supported_codecs.push_back(webrtc::SdpVideoFormat(
            webrtc::kAv1CodecName, webrtc::CodecParameterMap(), {webrtc::ScalabilityMode::kL1T1}));
#endif
    } else {
        // vp8
        supported_codecs.push_back(webrtc::SdpVideoFormat(webrtc::kVp8CodecName));
        // vp9
        auto supported_vp9_formats = webrtc::SupportedVP9Codecs(true);
        supported_codecs.insert(supported_codecs.end(), std::begin(supported_vp9_formats),
                                std::end(supported_vp9_formats));
        // av1
        supported_codecs.push_back(
            webrtc::SdpVideoFormat(webrtc::kAv1CodecName, webrtc::CodecParameterMap(),
                                   webrtc::LibaomAv1EncoderSupportedScalabilityModes()));
        // sw h264
        auto supported_h264_formats = webrtc::SupportedH264Codecs(true);
        supported_codecs.insert(supported_codecs.end(), std::begin(supported_h264_formats),
                                std::end(supported_h264_formats));
    }

    return supported_codecs;
}

std::unique_ptr<webrtc::VideoEncoder>
CustomVideoEncoderFactory::Create(const webrtc::Environment &env,
                                  const webrtc::SdpVideoFormat &format) {
    auto encoder = CreateEncoder(env, format);

    if (latency::Enabled()) {
        return CreateTracingVideoEncoder(std::move(encoder));
    }
    return encoder;
}

std::unique_ptr<webrtc::VideoEncoder>
CustomVideoEncoderFactory::CreateEncoder(const webrtc::Environment &env,
                                         const webrtc::SdpVideoFormat &format) {
#if defined(USE_JETSON_HW_ENCODER)
    if (args_.hw_accel) {
        return JetsonVideoEncoder::Create(args_);
    }
#endif

    if (absl::EqualsIgnoreCase(format.name, webrtc::kH264CodecName)) {
#if defined(USE_RPI_HW_ENCODER)
        if (args_.hw_accel) {
            return V4L2H264Encoder::Create(args_);
        }
#endif
        auto settings = webrtc::H264EncoderSettings::Parse(format);
        return webrtc::CreateH264Encoder(env, settings);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kVp8CodecName)) {
        return webrtc::CreateVp8Encoder(env);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kVp9CodecName)) {
        return webrtc::CreateVp9Encoder(env);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kAv1CodecName)) {
        return webrtc::CreateLibaomAv1Encoder(env);
    }

    return nullptr;
}
