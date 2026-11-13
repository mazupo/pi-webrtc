#ifndef ENCODER_FACTORY_H_
#define ENCODER_FACTORY_H_

#include <cstdint>
#include <memory>

#include <api/video_codecs/video_encoder_factory.h>

#include "codecs/frame_processor.h"
#include "codecs/rtc_encoder_config.h"

// WebRTC encoders: hardware when config.hw_accel is set and the device has one, software otherwise.
std::unique_ptr<webrtc::VideoEncoderFactory>
CreateRtcEncoderFactory(const RtcEncoderConfig &config);

// False when the device has no hardware encoder.
bool IsHwEncoderAvailable();

// Codec CreateVideoEncoder() produces: AV1 from the Jetson hardware encoder, H.264 otherwise.
uint32_t VideoEncoderFormat(bool use_hw);

// Hardware encoder when use_hw is set, OpenH264 otherwise; nullptr when it cannot be opened.
std::unique_ptr<FrameProcessor> CreateVideoEncoder(EncoderConfig config, bool use_hw);

#endif // ENCODER_FACTORY_H_
