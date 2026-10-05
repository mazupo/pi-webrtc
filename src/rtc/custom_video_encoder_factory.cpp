#include "rtc/custom_video_encoder_factory.h"

#include "common/latency_tracer.h"
#include "common/logging.h"
#include "rtc/tracing_video_encoder.h"

#include "codecs/h264/openh264_video_encoder.h"

#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_h264_encoder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_video_encoder.h"
#endif

#include <absl/algorithm/container.h>
#include <absl/strings/match.h>
#include <api/video_codecs/scalability_mode_helper.h>
#include <media/base/media_constants.h>
#include <modules/video_coding/codecs/av1/av1_svc_config.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp8/vp8_scalability.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

std::unique_ptr<webrtc::VideoEncoderFactory> CreateCustomVideoEncoderFactory(const Args &args) {
    return std::make_unique<CustomVideoEncoderFactory>(args);
}

CustomVideoEncoderFactory::CustomVideoEncoderFactory(const Args &args)
    : args_(args),
      hw_encoder_(args.hw_accel) {
#if defined(USE_RPI_HW_ENCODER)
    if (hw_encoder_ && !V4L2Encoder::IsAvailable()) {
        hw_encoder_ = false;
        WARN_PRINT("No hardware encoder found; WebRTC uses software encoding.");
    }
#endif
    if (hw_encoder_ && !args_.scalability_mode.empty()) {
        ERROR_PRINT("--scalability-mode is not supported by the hardware encoder.");
        exit(EXIT_FAILURE);
    }
}

std::vector<webrtc::SdpVideoFormat> CustomVideoEncoderFactory::GetSupportedFormats() const {
    std::vector<webrtc::SdpVideoFormat> supported_codecs;

    if (hw_encoder_) {
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
        absl::InlinedVector<webrtc::ScalabilityMode, webrtc::kScalabilityModeCount> vp8_modes(
            std::begin(webrtc::kVP8SupportedScalabilityModes),
            std::end(webrtc::kVP8SupportedScalabilityModes));
        supported_codecs.push_back(
            webrtc::SdpVideoFormat(webrtc::kVp8CodecName, webrtc::CodecParameterMap(), vp8_modes));
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

webrtc::VideoEncoderFactory::CodecSupport
CustomVideoEncoderFactory::QueryCodecSupport(const webrtc::SdpVideoFormat &format,
                                             std::optional<std::string> scalability_mode) const {
    std::optional<webrtc::ScalabilityMode> mode;
    if (scalability_mode) {
        mode = webrtc::ScalabilityModeStringToEnum(*scalability_mode);
        if (!mode) {
            return {};
        }
    }

    for (const auto &supported : GetSupportedFormats()) {
        if (!format.IsSameCodec(supported)) {
            continue;
        }
        if (!mode || absl::c_linear_search(supported.scalability_modes, *mode)) {
            return {.is_supported = true};
        }
    }
    return {};
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
    if (hw_encoder_) {
        return JetsonVideoEncoder::Create(args_);
    }
#endif

    if (absl::EqualsIgnoreCase(format.name, webrtc::kH264CodecName)) {
#if defined(USE_RPI_HW_ENCODER)
        if (hw_encoder_) {
            return V4L2H264Encoder::Create(args_);
        }
#endif
        return Openh264VideoEncoder::Create(args_);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kVp8CodecName)) {
        return webrtc::CreateVp8Encoder(env);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kVp9CodecName)) {
        return webrtc::CreateVp9Encoder(env);
    } else if (absl::EqualsIgnoreCase(format.name, webrtc::kAv1CodecName)) {
        return webrtc::CreateLibaomAv1Encoder(env);
    }

    return nullptr;
}
