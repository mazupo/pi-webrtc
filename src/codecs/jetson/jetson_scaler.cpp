#include "codecs/jetson/jetson_scaler.h"

#include <cstring>

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
      upload_surface_(nullptr),
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

    if (upload_surface_) {
        NvBufSurfaceUnMap(upload_surface_, 0, -1);
        NvBufSurfaceDestroy(upload_surface_);
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
    if (frame_buffer->GetDmaFd() > 0) {
        if (NvBufSurfaceFromFd(frame_buffer->GetDmaFd(), (void **)(&src_surface)) != 0) {
            src_surface = nullptr;
        }
    } else {
        src_surface = UploadToSurface(frame_buffer);
    }
    if (src_surface && NvBufSurfaceFromFd(dst_dma_fd, (void **)(&dst_surface)) == 0) {
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

NvBufSurface *JetsonScaler::UploadToSurface(const V4L2FrameBufferRef &frame_buffer) {
    const uint32_t format = frame_buffer->format();
    if (!upload_surface_) {
        NvBufSurfaceColorFormat color_format;
        if (format == V4L2_PIX_FMT_YUV420) {
            color_format = NVBUF_COLOR_FORMAT_YUV420;
        } else if (format == V4L2_PIX_FMT_YUYV) {
            color_format = NVBUF_COLOR_FORMAT_YUYV;
        } else {
            ERROR_PRINT("The Jetson scaler cannot read %s frames from CPU memory",
                        v4l2_util::FourccToString(format).c_str());
            abort_ = true;
            return nullptr;
        }

        NvBufSurfaceAllocateParams params{};
        params.params.width = config_.src_width;
        params.params.height = config_.src_height;
        params.params.layout = NVBUF_LAYOUT_PITCH;
        params.params.colorFormat = color_format;
        params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
        params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

        if (NvBufSurfaceAllocate(&upload_surface_, 1, &params) != 0) {
            ERROR_PRINT("Failed to allocate the NvBuffer for CPU frames");
            upload_surface_ = nullptr;
            abort_ = true;
            return nullptr;
        }
        upload_surface_->numFilled = 1;

        if (NvBufSurfaceMap(upload_surface_, 0, -1, NVBUF_MAP_WRITE) != 0) {
            ERROR_PRINT("Failed to map the NvBuffer for CPU frames");
            abort_ = true;
            return nullptr;
        }
        INFO_PRINT("Copying %s frames from CPU memory to the hardware scaler",
                   v4l2_util::FourccToString(format).c_str());
    }

    // Where each plane of the frame starts, and its bytes per row.
    const auto *src = static_cast<const uint8_t *>(frame_buffer->Data());
    const int stride = frame_buffer->stride();
    const int plane_height = frame_buffer->plane_height();
    const uint8_t *src_planes[3] = {src, nullptr, nullptr};
    int src_pitches[3] = {stride, stride / 2, stride / 2};
    uint32_t frame_size = stride * plane_height * 3 / 2;
    if (format == V4L2_PIX_FMT_YUYV) {
        src_pitches[0] = stride * 2;
        frame_size = stride * 2 * plane_height;
    } else {
        src_planes[1] = src + stride * plane_height;
        src_planes[2] = src_planes[1] + (stride / 2) * (plane_height / 2);
    }
    if (!src || frame_buffer->size() < frame_size) {
        ERROR_PRINT("Dropped a %u-byte frame; a %dx%d %s frame needs %u bytes",
                    frame_buffer->size(), config_.src_width, config_.src_height,
                    v4l2_util::FourccToString(format).c_str(), frame_size);
        return nullptr;
    }

    // The NvBuffer pads each row to its own pitch, so copy one row at a time.
    NvBufSurfaceParams &surface = upload_surface_->surfaceList[0];
    for (uint32_t p = 0; p < surface.planeParams.num_planes; ++p) {
        auto *dst = static_cast<uint8_t *>(surface.mappedAddr.addr[p]);
        const uint32_t row_bytes =
            surface.planeParams.width[p] * surface.planeParams.bytesPerPix[p];
        for (uint32_t row = 0; row < surface.planeParams.height[p]; ++row) {
            memcpy(dst + row * surface.planeParams.pitch[p], src_planes[p] + row * src_pitches[p],
                   row_bytes);
        }
    }
    NvBufSurfaceSyncForDevice(upload_surface_, 0, -1);

    return upload_surface_;
}

void JetsonScaler::CaptureBuffer() {
    if (auto task = capturing_tasks_.pop(1)) {
        if (task->queued_us != 0) {
            latency::RecordSince(latency::Stage::kScalerDwell, task->queued_us);
        }
        task->callback();
    }
}
