#ifndef CUSTOM_VIDEO_ENCODER_FACTORY_H_
#define CUSTOM_VIDEO_ENCODER_FACTORY_H_

#include <api/environment/environment.h>
#include <api/video_codecs/video_encoder_factory.h>

#include "codecs/rtc_encoder_config.h"

std::unique_ptr<webrtc::VideoEncoderFactory>
CreateCustomVideoEncoderFactory(const RtcEncoderConfig &config);

class CustomVideoEncoderFactory : public webrtc::VideoEncoderFactory {
  public:
    CustomVideoEncoderFactory(const RtcEncoderConfig &config);
    ~CustomVideoEncoderFactory() = default;

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
    CodecSupport QueryCodecSupport(const webrtc::SdpVideoFormat &format,
                                   std::optional<std::string> scalability_mode) const override;

    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment &env,
                                                 const webrtc::SdpVideoFormat &format) override;

  private:
    std::unique_ptr<webrtc::VideoEncoder> CreateEncoder(const webrtc::Environment &env,
                                                        const webrtc::SdpVideoFormat &format);

    RtcEncoderConfig config_;
    bool hw_encoder_;
};

#endif // CUSTOM_VIDEO_ENCODER_FACTORY_H_
