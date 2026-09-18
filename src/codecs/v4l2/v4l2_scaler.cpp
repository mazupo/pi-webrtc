#include "codecs/v4l2/v4l2_scaler.h"
#include "common/logging.h"

constexpr const char *SCALER_FILE = "/dev/video12";
constexpr int BUFFER_NUM = 2;

std::unique_ptr<V4L2Scaler> V4L2Scaler::Create(ScalerConfig config) {
    auto scaler = std::make_unique<V4L2Scaler>(config);
    if (!scaler->Initialize()) {
        return nullptr;
    }
    scaler->Start();
    return scaler;
}

bool V4L2Scaler::IsAvailable() { return v4l2_util::IsM2MDeviceReady(SCALER_FILE); }

V4L2Scaler::V4L2Scaler(ScalerConfig config)
    : V4L2Codec(),
      config_(config) {
    dwell_stage_ = latency::Stage::kScalerDwell;
}

bool V4L2Scaler::Initialize() {
    if (!Open(SCALER_FILE)) {
        ERROR_PRINT("Unable to turn on scaler: %s", SCALER_FILE);
        return false;
    }

    auto src_memory = config_.is_dma_src ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    if (!SetupOutputBuffer(config_.src_width, config_.src_height, config_.src_pix_fmt, src_memory,
                           BUFFER_NUM)) {
        ERROR_PRINT("Could not setup output buffer");
        return false;
    }
    if (!SetupCaptureBuffer(config_.dst_width, config_.dst_height, V4L2_PIX_FMT_YUV420,
                            V4L2_MEMORY_MMAP, BUFFER_NUM, config_.is_dma_dst)) {
        ERROR_PRINT("Could not setup capture buffer");
        return false;
    }

    return true;
}
