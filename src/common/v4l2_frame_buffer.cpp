#include "common/v4l2_frame_buffer.h"
#include "common/logging.h"

#include <third_party/libyuv/include/libyuv.h>
#if defined(USE_LIBARGUS_CAPTURE)
#include <nvbufsurface.h>
#include <nvbufsurftransform.h>
#endif

#include <chrono>
#include <cstring>

namespace {

// Aligning pointer to 64 bytes for improved performance, e.g. use SIMD.
const int kBufferAlignment = 64;

#if defined(USE_LIBARGUS_CAPTURE)

int ReadDmaBuffer(int src_dma_fd, uint8_t *dst_addr, size_t dst_size) {
    if (src_dma_fd <= 0)
        return -1;

    int ret = -1;

    NvBufSurface *nvbuf_surf = 0;
    ret = NvBufSurfaceFromFd(src_dma_fd, (void **)(&nvbuf_surf));
    if (ret != 0) {
        return -1;
    }

    int offset = 0;

    for (int plane = 0; plane < nvbuf_surf->surfaceList->planeParams.num_planes; ++plane) {
        NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ);
        NvBufSurfaceSyncForCpu(nvbuf_surf, 0, plane);

        uint8_t *src_addr = static_cast<uint8_t *>(nvbuf_surf->surfaceList->mappedAddr.addr[plane]);
        int row_size = nvbuf_surf->surfaceList->planeParams.width[plane] *
                       nvbuf_surf->surfaceList->planeParams.bytesPerPix[plane];

        for (uint row = 0; row < nvbuf_surf->surfaceList->planeParams.height[plane]; ++row) {
            memcpy(dst_addr + offset,
                   src_addr + row * nvbuf_surf->surfaceList->planeParams.pitch[plane], row_size);
            offset += row_size;
        }

        NvBufSurfaceSyncForDevice(nvbuf_surf, 0, plane);
        ret = NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
        if (ret < 0) {
            ERROR_PRINT("Error while Unmapping buffer");
            return ret;
        }
    }

    return 0;
}

int NvConvertToI420(int src_dma_fd, uint8_t *dst_addr, size_t dst_size, int width, int height) {
    NvBufSurface *src_surface = nullptr;
    if (NvBufSurfaceFromFd(src_dma_fd, (void **)(&src_surface)) != 0) {
        return -1;
    }

    NvBufSurfaceAllocateParams params{};
    params.params.width = width;
    params.params.height = height;
    params.params.layout = NVBUF_LAYOUT_PITCH;
    params.params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.memtag = NvBufSurfaceTag_CAMERA;

    NvBufSurface *dst_surface = nullptr;
    if (NvBufSurfaceAllocate(&dst_surface, 1, &params) != 0) {
        return -1;
    }
    dst_surface->numFilled = 1;

    NvBufSurfTransformParams transform_params{};
    transform_params.transform_flag = NVBUFSURF_TRANSFORM_FILTER;
    transform_params.transform_flip = NvBufSurfTransform_None;
    transform_params.transform_filter = NvBufSurfTransformInter_Algo3;

    int ret = NvBufSurfTransform(src_surface, dst_surface, &transform_params);
    if (ret == NvBufSurfTransformError_Success) {
        ret = ReadDmaBuffer(static_cast<int>(dst_surface->surfaceList[0].bufferDesc), dst_addr,
                            dst_size);
    }

    NvBufSurfaceDestroy(dst_surface);

    return ret;
}

#endif // USE_LIBARGUS_CAPTURE

} // namespace

webrtc::scoped_refptr<V4L2FrameBuffer> V4L2FrameBuffer::Create(int width, int height, int size,
                                                               uint32_t format) {
    return webrtc::make_ref_counted<V4L2FrameBuffer>(width, height, size, format);
}

webrtc::scoped_refptr<V4L2FrameBuffer> V4L2FrameBuffer::Create(int width, int height,
                                                               V4L2Buffer buffer) {
    return webrtc::make_ref_counted<V4L2FrameBuffer>(width, height, buffer);
}

V4L2FrameBuffer::V4L2FrameBuffer(int width, int height, uint32_t format, int size, uint32_t flags,
                                 timeval timestamp)
    : width_(width),
      height_(height),
      format_(format),
      size_(size),
      flags_(flags),
      timestamp_(timestamp),
      buffer_({}),
      data_(nullptr) {}

V4L2FrameBuffer::V4L2FrameBuffer(int width, int height, V4L2Buffer buffer)
    : V4L2FrameBuffer(width, height, buffer.pix_fmt, buffer.length, buffer.flags,
                      buffer.timestamp) {
    buffer_ = buffer;
}

V4L2FrameBuffer::V4L2FrameBuffer(int width, int height, int size, uint32_t format)
    : V4L2FrameBuffer(width, height, format, size, 0, {0, 0}) {
    data_.reset(static_cast<uint8_t *>(webrtc::AlignedMalloc(size_, kBufferAlignment)));
}

V4L2FrameBuffer::~V4L2FrameBuffer() {}

webrtc::VideoFrameBuffer::Type V4L2FrameBuffer::type() const { return Type::kNative; }

int V4L2FrameBuffer::width() const { return width_; }
int V4L2FrameBuffer::height() const { return height_; }
uint32_t V4L2FrameBuffer::format() const { return format_; }
uint32_t V4L2FrameBuffer::size() const { return size_; }
uint32_t V4L2FrameBuffer::flags() const { return flags_; }
timeval V4L2FrameBuffer::timestamp() const { return timestamp_; }

webrtc::scoped_refptr<webrtc::I420BufferInterface> V4L2FrameBuffer::ToI420() {
    webrtc::scoped_refptr<webrtc::I420Buffer> i420_buffer(
        webrtc::I420Buffer::Create(width_, height_));
    i420_buffer->InitializeData();

    const uint8_t *src = static_cast<const uint8_t *>(Data());

    if (format_ == V4L2_PIX_FMT_YUV420) {
        memcpy(i420_buffer->MutableDataY(), src, size_);
        return i420_buffer;
    }

#if defined(USE_LIBARGUS_CAPTURE)
    if (IsDmaOnly()) {
        if (NvConvertToI420(buffer_.dmafd, i420_buffer->MutableDataY(), size_, width_, height_) <
            0) {
            ERROR_PRINT("NvConvertToI420 Failed");
        }
        return i420_buffer;
    }
#endif

    if (libyuv::ConvertToI420(src, size_, i420_buffer->MutableDataY(), i420_buffer->StrideY(),
                              i420_buffer->MutableDataU(), i420_buffer->StrideU(),
                              i420_buffer->MutableDataV(), i420_buffer->StrideV(), 0, 0, width_,
                              height_, width_, height_, libyuv::kRotate0, format_) < 0) {
        ERROR_PRINT("libyuv ConvertToI420 Failed");
    }

    return i420_buffer;
}

V4L2Buffer V4L2FrameBuffer::GetRawBuffer() { return buffer_; }

const void *V4L2FrameBuffer::Data() const { return data_ ? data_.get() : buffer_.start; }

bool V4L2FrameBuffer::IsDmaOnly() const { return Data() == nullptr && buffer_.dmafd > 0; }

uint8_t *V4L2FrameBuffer::MutableData() {
    if (!data_) {
        throw std::runtime_error(
            "MutableData() is not supported for frames directly created from V4L2 buffers. Use "
            "Clone() to create an owning (writable) copy before calling MutableData().");
    }
    return data_.get();
}

int V4L2FrameBuffer::GetDmaFd() const { return buffer_.dmafd; }

void V4L2FrameBuffer::SetDmaFd(int fd) {
    if (fd > 0) {
        buffer_.dmafd = fd;
    }
}

void V4L2FrameBuffer::SetTimestamp(timeval timestamp) { timestamp_ = timestamp; }

webrtc::scoped_refptr<V4L2FrameBuffer> V4L2FrameBuffer::Clone() const {
    auto clone = webrtc::make_ref_counted<V4L2FrameBuffer>(width_, height_, size_, format_);

    memcpy(clone->MutableData(), Data(), size_);

    clone->SetDmaFd(buffer_.dmafd);
    clone->flags_ = flags_;
    clone->timestamp_ = timestamp_;

    return clone;
}
