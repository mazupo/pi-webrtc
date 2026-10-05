#include "recorder/v4l2_h264_recorder.h"
#include "common/logging.h"

std::unique_ptr<V4L2H264Recorder> V4L2H264Recorder::Create(int width, int height, int fps) {
    if (!V4L2Encoder::IsAvailable()) {
        WARN_PRINT("No hardware encoder found; recording with OpenH264.");
        return nullptr;
    }
    return std::make_unique<V4L2H264Recorder>(width, height, fps);
}

V4L2H264Recorder::V4L2H264Recorder(int width, int height, int fps)
    : VideoRecorder(width, height, fps, AV_CODEC_ID_H264) {}

void V4L2H264Recorder::Encode(V4L2FrameBufferRef frame_buffer) {
    if (!encoder_) {
        EncoderConfig config = {
            .width = width,
            .height = height,
            .fps = fps,
            .bitrate = static_cast<int>(width * height * fps * 0.1),
            .keyframe_interval = 30,
            .is_dma_src = false,
            .src_pix_fmt = frame_buffer->format(),
            .src_plane_height = frame_buffer->plane_height(),
            .rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
        };
        encoder_ = V4L2Encoder::Create(config);
        encoder_->ForceKeyFrame();
    }

    encoder_->EmplaceBuffer(frame_buffer, [this, frame_buffer](V4L2FrameBufferRef encoded_buffer) {
        OnEncoded((uint8_t *)encoded_buffer->Data(), encoded_buffer->size(),
                  frame_buffer->timestamp(), encoded_buffer->flags());
    });
}

void V4L2H264Recorder::ReleaseEncoder() { encoder_.reset(); }
