#include "recorder/openh264_recorder.h"

std::unique_ptr<Openh264Recorder> Openh264Recorder::Create(int width, int height, int fps) {
    return std::make_unique<Openh264Recorder>(width, height, fps);
}

Openh264Recorder::Openh264Recorder(int width, int height, int fps)
    : VideoRecorder(width, height, fps, AV_CODEC_ID_H264) {}

void Openh264Recorder::Encode(V4L2FrameBufferRef frame_buffer) {
    if (!encoder_) {
        EncoderConfig config = {
            .width = width,
            .height = height,
            .fps = fps,
            .bitrate = static_cast<int>(width * height * fps * 0.1),
            .keyframe_interval = fps,
            .idr_interval = fps,
            .rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
        };
        encoder_ = Openh264Encoder::Create(config);
    }

    encoder_->EmplaceBuffer(frame_buffer, [this, frame_buffer](V4L2FrameBufferRef encoded_buffer) {
        OnEncoded((uint8_t *)encoded_buffer->Data(), encoded_buffer->size(),
                  frame_buffer->timestamp(), encoded_buffer->flags());
    });
}

void Openh264Recorder::ReleaseEncoder() { encoder_.reset(); }
