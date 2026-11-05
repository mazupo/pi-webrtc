#ifndef ENCODER_FACTORY_H_
#define ENCODER_FACTORY_H_

#include <memory>

#include <api/video_codecs/video_encoder_factory.h>

#include "codecs/rtc_encoder_config.h"

// WebRTC encoders: hardware when config.hw_accel is set and the device has one, software otherwise.
std::unique_ptr<webrtc::VideoEncoderFactory>
CreateRtcEncoderFactory(const RtcEncoderConfig &config);

#endif // ENCODER_FACTORY_H_
