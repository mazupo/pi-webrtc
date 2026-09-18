#include "codecs/v4l2/v4l2_encoder.h"
#include "common/logging.h"

constexpr const char *ENCODER_FILE = "/dev/video11";
constexpr int BUFFER_NUM = 2;

std::unique_ptr<V4L2Encoder> V4L2Encoder::Create(EncoderConfig config) {
    auto encoder = std::make_unique<V4L2Encoder>(config);
    if (!encoder->Initialize()) {
        return nullptr;
    }
    encoder->Start();
    return encoder;
}

bool V4L2Encoder::IsAvailable() { return v4l2_util::IsM2MDeviceReady(ENCODER_FILE); }

V4L2Encoder::V4L2Encoder(EncoderConfig config)
    : V4L2Codec(),
      config_(config) {}

bool V4L2Encoder::Initialize() {
    if (!Open(ENCODER_FILE)) {
        ERROR_PRINT("Unable to turn on encoder: %s", ENCODER_FILE);
        return false;
    }

    auto src_memory = config_.is_dma_src ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    if (!SetupOutputBuffer(config_.width, config_.height, config_.src_pix_fmt, src_memory,
                           BUFFER_NUM)) {
        ERROR_PRINT("Could not setup output buffer");
        return false;
    }
    if (!SetupCaptureBuffer(config_.width, config_.height, V4L2_PIX_FMT_H264, V4L2_MEMORY_MMAP,
                            BUFFER_NUM)) {
        ERROR_PRINT("Could not setup capture buffer");
        return false;
    }

    SetProfile(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
    SetLevel(V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
    SetIFrameInterval(config_.keyframe_interval);
    SetRateControlMode(config_.rc_mode);

    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_BITRATE, config_.bitrate)) {
        ERROR_PRINT("Could not set bitrate");
    }

    if (!V4L2Codec::SetFps(config_.fps)) {
        ERROR_PRINT("Failed to set output fps: %d", config_.fps);
    }

    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, true)) {
        ERROR_PRINT("Could not set repeat seq header");
    }

    return true;
}

void V4L2Encoder::ForceKeyFrame() {
    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1)) {
        ERROR_PRINT("Could not force v4l2 encoder to keyframe");
    }
}

void V4L2Encoder::SetLevel(uint32_t level) {
    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, level)) {
        ERROR_PRINT("Could not set h264 level");
    }
}
void V4L2Encoder::SetProfile(uint32_t profile) {
    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, profile)) {
        ERROR_PRINT("Could not set h264 profile to baseline");
    }
}

void V4L2Encoder::SetFps(uint32_t adjusted_fps) {
    if (config_.fps != adjusted_fps) {
        config_.fps = adjusted_fps;
        if (!V4L2Codec::SetFps(config_.fps)) {
            ERROR_PRINT("Failed to set output fps: %d", config_.fps);
        }
    }
}

void V4L2Encoder::SetRateControlMode(uint32_t mode) {
    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, mode)) {
        ERROR_PRINT("Could not set rate control mode");
    }
}
void V4L2Encoder::SetIFrameInterval(uint32_t interval) {
    if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, interval)) {
        ERROR_PRINT("Could not set i-frame interval");
    }
}

void V4L2Encoder::SetBitrate(uint32_t adjusted_bitrate_bps) {
    if (adjusted_bitrate_bps < 1000000) {
        adjusted_bitrate_bps = 1000000;
    } else {
        adjusted_bitrate_bps = (adjusted_bitrate_bps / 25000) * 25000;
    }

    if (config_.bitrate != adjusted_bitrate_bps) {
        config_.bitrate = adjusted_bitrate_bps;
        if (!SetExtCtrl(V4L2_CID_MPEG_VIDEO_BITRATE, config_.bitrate)) {
            ERROR_PRINT("Failed to set bitrate: %d bps", config_.bitrate);
        }
    }
}
