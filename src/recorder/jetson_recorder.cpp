#include "recorder/jetson_recorder.h"

const float bpp_factor = 0.06f;

std::unique_ptr<JetsonRecorder> JetsonRecorder::Create(int width, int height, int fps) {
    return std::make_unique<JetsonRecorder>(width, height, fps);
}

JetsonRecorder::JetsonRecorder(int width, int height, int fps)
    : VideoRecorder(width, height, fps, AV_CODEC_ID_AV1) {}

void JetsonRecorder::Encode(V4L2FrameBufferRef frame_buffer) {
    if (!encoder_) {
        EncoderConfig config = {
            .width = width,
            .height = height,
            .fps = fps,
            .bitrate = static_cast<int>(width * height * fps * bpp_factor),
            .keyframe_interval = fps,
            .idr_interval = fps,
            .is_dma_src = true,
            .dst_pix_fmt = V4L2_PIX_FMT_AV1,
            .rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
        };

        encoder_ = JetsonEncoder::Create(config);
    }

    encoder_->EmplaceBuffer(frame_buffer, [this, frame_buffer](V4L2FrameBufferRef encoded_buffer) {
        OnEncoded((uint8_t *)encoded_buffer->Data(), encoded_buffer->size(),
                  frame_buffer->timestamp(), encoded_buffer->flags());
    });
}

void JetsonRecorder::ReleaseEncoder() { encoder_.reset(); }
