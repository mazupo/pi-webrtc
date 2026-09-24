#include "codecs/jetson/jetson_scaler.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_utils.h"

std::unique_ptr<JetsonScaler> JetsonScaler::Create(ScalerConfig config) {
    auto scaler = std::make_unique<JetsonScaler>(config);
    if (!scaler->Initialize()) {
        return nullptr;
    }
    scaler->Start();
    return scaler;
}

JetsonScaler::JetsonScaler(ScalerConfig config)
    : config_(config),
      num_buffer_(2),
      abort_(false),
      free_buffers_(num_buffer_),
      capturing_tasks_(num_buffer_) {}

JetsonScaler::~JetsonScaler() {
    abort_ = true;
    worker_.reset();

    while (auto task = capturing_tasks_.pop()) {
        // Return the unused dma fd back to free buffers
        free_buffers_.push(task->dst_dma_fd);
    }

    while (auto item = free_buffers_.pop()) {
        NvBufSurface *surface = nullptr;
        if (NvBufSurfaceFromFd(item.value(), (void **)(&surface)) != 0 ||
            NvBufSurfaceDestroy(surface) != 0) {
            ERROR_PRINT("Failed to Destroy NvBuffer");
        }
    }

    DEBUG_PRINT("~JetsonScaler");
}

bool JetsonScaler::Initialize() {
    src_rect_ = {};
    src_rect_.width = config_.src_width;
    src_rect_.height = config_.src_height;
    dst_rect_ = {};
    dst_rect_.width = config_.dst_width;
    dst_rect_.height = config_.dst_height;

    transform_params_ = {};
    transform_params_.transform_flip = NvBufSurfTransform_None;
    transform_params_.transform_filter = NvBufSurfTransformInter_Nearest;
    transform_params_.src_rect = &src_rect_;
    transform_params_.dst_rect = &dst_rect_;

    for (int i = 0; i < num_buffer_; ++i) {
        NvBufSurfaceAllocateParams params{};
        params.params.width = config_.dst_width;
        params.params.height = config_.dst_height;
        params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
        params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
        params.memtag = NvBufSurfaceTag_VIDEO_ENC;

        NvBufSurface *surface = nullptr;
        if (NvBufSurfaceAllocate(&surface, 1, &params) != 0) {
            ERROR_PRINT("Failed to allocate NvBuffer");
            return false;
        }
        surface->numFilled = 1;

        free_buffers_.push(static_cast<int>(surface->surfaceList[0].bufferDesc));
    }

    return true;
}

void JetsonScaler::Start() {
    worker_ = std::make_unique<Worker>("NvTransform", [this]() {
        CaptureBuffer();
    });
    worker_->Run();
}

void JetsonScaler::EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                                 std::function<void(V4L2FrameBufferRef)> on_capture) {
    if (abort_) {
        return;
    }

    const bool traced = latency::Enabled();

    auto item = free_buffers_.pop();
    if (!item) {
        if (traced) {
            latency::Count(latency::Counter::kScalerNoBuffer);
        }
        return;
    }

    int dst_dma_fd = item.value();

    const int64_t transform_start_us = traced ? latency::NowUs() : 0;
    NvBufSurface *src_surface = nullptr;
    NvBufSurface *dst_surface = nullptr;
    int ret = -1;
    if (NvBufSurfaceFromFd(frame_buffer->GetDmaFd(), (void **)(&src_surface)) == 0 &&
        NvBufSurfaceFromFd(dst_dma_fd, (void **)(&dst_surface)) == 0) {
        ret = NvBufSurfTransform(src_surface, dst_surface, &transform_params_);
    }
    if (traced) {
        latency::RecordSince(latency::Stage::kNvTransform, transform_start_us);
    }
    if (ret < 0) {
        ERROR_PRINT("NvBufSurfTransform failed to tranform from fd(%d) to fd(%d)",
                    frame_buffer->GetDmaFd(), dst_dma_fd);
        free_buffers_.push(dst_dma_fd);
        return;
    }

    CaptureTask task;
    task.dst_dma_fd = dst_dma_fd;
    task.callback = [this, dst_dma_fd, on_capture]() {
        auto v4l2_buffer =
            V4L2Buffer::FromCapturedPlane(nullptr, 0, dst_dma_fd, 0, V4L2_PIX_FMT_NV12);
        auto scaled_frame =
            V4L2FrameBuffer::Create(config_.dst_width, config_.dst_height, v4l2_buffer);
        on_capture(scaled_frame);
        free_buffers_.push(dst_dma_fd);
    };

    if (traced) {
        task.queued_us = latency::NowUs();
    }

    if (!capturing_tasks_.push(std::move(task))) {
        free_buffers_.push(dst_dma_fd);
        if (traced) {
            latency::Count(latency::Counter::kScalerQueueFull);
        }
    }
}

void JetsonScaler::CaptureBuffer() {
    if (auto task = capturing_tasks_.pop(1)) {
        if (task->queued_us != 0) {
            latency::RecordSince(latency::Stage::kScalerDwell, task->queued_us);
        }
        task->callback();
    }
}
