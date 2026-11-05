#ifndef TRACING_VIDEO_ENCODER_H_
#define TRACING_VIDEO_ENCODER_H_

#include <memory>

#include <api/video_codecs/video_encoder.h>

// Wraps any VideoEncoder to time the encode step and the handoff to WebRTC. Wrapping the factory's
// result covers all four pipelines at once: the built-in OpenH264 encoder, V4L2H264Encoder and
// JetsonVideoEncoder need no changes of their own.
//
// It also wraps the registered EncodedImageCallback, which is what catches the asynchronous
// hardware encoders: their Encode() returns immediately and the encoded frame arrives later on a
// dequeue thread, so the only place both ends are visible is around the callback.
std::unique_ptr<webrtc::VideoEncoder>
CreateTracingVideoEncoder(std::unique_ptr<webrtc::VideoEncoder> encoder);

#endif // TRACING_VIDEO_ENCODER_H_
