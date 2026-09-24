#include "codecs/jetson/jetson_decoder.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_utils.h"

#include <algorithm>
#include <cstring>

#include "Error.h"

namespace {

const int INPUT_BUFFER_NUM = 2;
const int EXTRA_CAPTURE_BUFFER_NUM = 1;
const int FRAME_BUFFER_NUM = 2;
const uint32_t MIN_INPUT_SIZE = 2 * 1024 * 1024;

std::atomic<uint32_t> global_dec_id{0};

} // namespace

std::unique_ptr<JetsonDecoder> JetsonDecoder::Create(DecoderConfig config) {
    auto decoder = std::make_unique<JetsonDecoder>(
        config, "dec" + std::to_string(global_dec_id.fetch_add(1) % 10));
    if (!decoder->Initialize()) {
        return nullptr;
    }
    decoder->Start();
    return decoder;
}

JetsonDecoder::JetsonDecoder(DecoderConfig config, std::string name)
    : decoder_(nullptr),
      name_(std::move(name)),
      config_(config),
      frame_size_(config.width * config.height * 3 / 2),
      abort_(true),
      capture_ready_(false),
      src_rect_({}),
      dst_rect_({}),
      transform_params_({}),
      free_buffers_(FRAME_BUFFER_NUM) {}

JetsonDecoder::~JetsonDecoder() {
    abort_ = true;

    if (decoder_) {
        if (capture_ready_) {
            SendEOS();
            decoder_->capture_plane.waitForDQThread(2000);
            decoder_->capture_plane.deinitPlane();
        }
        decoder_->output_plane.deinitPlane();
        delete decoder_;
        decoder_ = nullptr;
    }

    while (auto item = free_buffers_.pop()) {
        NvBufSurface *surface = nullptr;
        if (NvBufSurfaceFromFd(item.value(), (void **)(&surface)) != 0 ||
            NvBufSurfaceDestroy(surface) != 0) {
            ERROR_PRINT("Failed to destroy NvBuffer");
        }
    }

    DEBUG_PRINT("~JetsonDecoder");
}

bool JetsonDecoder::Initialize() {
    if (config_.src_pix_fmt != V4L2_PIX_FMT_MJPEG && config_.src_pix_fmt != V4L2_PIX_FMT_H264) {
        ERROR_PRINT("Unsupported source format: %s",
                    v4l2_util::FourccToString(config_.src_pix_fmt).c_str());
        return false;
    }
    if (!CreateVideoDecoder()) {
        ERROR_PRINT("Failed to create video decoder");
        return false;
    }
    if (!AllocateFrameBuffers()) {
        return false;
    }
    return true;
}

bool JetsonDecoder::CreateVideoDecoder() {
    decoder_ = NvVideoDecoder::createVideoDecoder(name_.c_str());
    if (!decoder_)
        ORIGINATE_ERROR("Could not create decoder");

    if (decoder_->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0) < 0)
        ORIGINATE_ERROR("Could not subscribe resolution change event");

    uint32_t input_size = std::max(frame_size_, MIN_INPUT_SIZE);
    if (decoder_->setOutputPlaneFormat(config_.src_pix_fmt, input_size) < 0)
        ORIGINATE_ERROR("Could not set output plane format");

    if (decoder_->setFrameInputMode(0) < 0)
        ORIGINATE_ERROR("Could not set frame input mode");

    if (decoder_->output_plane.setupPlane(V4L2_MEMORY_MMAP, INPUT_BUFFER_NUM, true, false) < 0)
        ORIGINATE_ERROR("Could not setup output plane");

    return true;
}

bool JetsonDecoder::AllocateFrameBuffers() {
    src_rect_.width = config_.width;
    src_rect_.height = config_.height;
    dst_rect_.width = config_.width;
    dst_rect_.height = config_.height;
    transform_params_.transform_flip = NvBufSurfTransform_None;
    transform_params_.transform_filter = NvBufSurfTransformInter_Nearest;
    transform_params_.src_rect = &src_rect_;
    transform_params_.dst_rect = &dst_rect_;

    for (int i = 0; i < FRAME_BUFFER_NUM; ++i) {
        NvBufSurfaceAllocateParams params{};
        params.params.width = config_.width;
        params.params.height = config_.height;
        params.params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
        params.params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
        params.memtag = NvBufSurfaceTag_VIDEO_DEC;

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

void JetsonDecoder::Start() {
    if (decoder_->output_plane.setStreamStatus(true) < 0) {
        ERROR_PRINT("Failed to stream on output plane");
        return;
    }

    abort_ = false;
}

bool JetsonDecoder::EnsureCapturePlane() {
    if (capture_ready_) {
        return true;
    }

    struct v4l2_event ev;
    memset(&ev, 0, sizeof(ev));
    if (decoder_->dqEvent(ev, 1000) < 0 || ev.type != V4L2_EVENT_RESOLUTION_CHANGE)
        ORIGINATE_ERROR("Could not get the resolution of the decoded stream");

    struct v4l2_format format;
    struct v4l2_crop crop;
    if (decoder_->capture_plane.getFormat(format) < 0)
        ORIGINATE_ERROR("Could not get capture plane format");
    if (decoder_->capture_plane.getCrop(crop) < 0)
        ORIGINATE_ERROR("Could not get capture plane crop");

    INFO_PRINT("Decoded %s stream: %s(%dx%d)",
               v4l2_util::FourccToString(config_.src_pix_fmt).c_str(),
               v4l2_util::FourccToString(format.fmt.pix_mp.pixelformat).c_str(), crop.c.width,
               crop.c.height);

    if (decoder_->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat, format.fmt.pix_mp.width,
                                        format.fmt.pix_mp.height) < 0)
        ORIGINATE_ERROR("Could not set capture plane format");

    src_rect_.width = crop.c.width;
    src_rect_.height = crop.c.height;

    int min_buffers = 0;
    if (decoder_->getMinimumCapturePlaneBuffers(min_buffers) < 0)
        ORIGINATE_ERROR("Could not get the minimum capture plane buffers");

    const int num_buffers = min_buffers + EXTRA_CAPTURE_BUFFER_NUM;

    if (decoder_->capture_plane.setupPlane(V4L2_MEMORY_MMAP, num_buffers, false, false) < 0)
        ORIGINATE_ERROR("Could not setup capture plane");

    if (decoder_->capture_plane.setStreamStatus(true) < 0)
        ORIGINATE_ERROR("Failed to stream on capture plane");

    decoder_->capture_plane.setDQThreadCallback(CapturePlaneDqCallback);
    decoder_->capture_plane.startDQThread(this);

    if (!PrepareCaptureBuffer()) {
        return false;
    }

    capture_ready_ = true;

    return true;
}

bool JetsonDecoder::PrepareCaptureBuffer() {
    for (uint32_t i = 0; i < decoder_->capture_plane.getNumBuffers(); i++) {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;

        if (decoder_->capture_plane.qBuffer(v4l2_buf, nullptr) < 0)
            ORIGINATE_ERROR("Failed to queue buffers into decoder capture plane");
    }

    return true;
}

void JetsonDecoder::EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                                  std::function<void(V4L2FrameBufferRef)> on_capture) {
    if (abort_ || decoder_->isInError()) {
        return;
    }

    struct v4l2_buffer v4l2_output_buf;
    struct v4l2_plane output_planes[MAX_PLANES];
    NvBuffer *nv_buffer;

    memset(&v4l2_output_buf, 0, sizeof(v4l2_output_buf));
    memset(output_planes, 0, sizeof(output_planes));
    v4l2_output_buf.m.planes = output_planes;

    if (decoder_->output_plane.getNumQueuedBuffers() == decoder_->output_plane.getNumBuffers()) {
        if (decoder_->output_plane.dqBuffer(v4l2_output_buf, &nv_buffer, nullptr, 10) < 0) {
            if (latency::Enabled()) {
                latency::Count(latency::Counter::kDecoderDqTimeout);
            }
            ERROR_PRINT("Failed to dqBuffer at decoder output_plane");
            return;
        }
    } else {
        nv_buffer =
            decoder_->output_plane.getNthBuffer(decoder_->output_plane.getNumQueuedBuffers());
        v4l2_output_buf.index = nv_buffer->index;
    }

    uint32_t bytesused = frame_buffer->size();
    if (bytesused > nv_buffer->planes[0].length) {
        ERROR_PRINT("Compressed frame (%u bytes) exceeds the decoder input buffer (%u bytes)",
                    bytesused, nv_buffer->planes[0].length);
        return;
    }
    memcpy(nv_buffer->planes[0].data, frame_buffer->Data(), bytesused);
    nv_buffer->planes[0].bytesused = bytesused;
    v4l2_output_buf.m.planes[0].bytesused = bytesused;

    if (decoder_->output_plane.qBuffer(v4l2_output_buf, nullptr) < 0) {
        ERROR_PRINT("Failed to qBuffer at decoder output_plane");
        return;
    }

    if (!EnsureCapturePlane()) {
        abort_ = true;
        return;
    }

    if (latency::Enabled()) {
        const int64_t queued_us = latency::NowUs();
        capturing_tasks_.push([on_capture, queued_us](V4L2FrameBufferRef decoded_buffer) {
            latency::RecordSince(latency::Stage::kHwDecodeDwell, queued_us);
            on_capture(decoded_buffer);
        });
        return;
    }

    capturing_tasks_.push(on_capture);
}

void JetsonDecoder::Transform(int src_dma_fd,
                              const std::function<void(V4L2FrameBufferRef)> &on_capture) {
    const bool traced = latency::Enabled();

    auto item = free_buffers_.pop();
    if (!item) {
        if (traced) {
            latency::Count(latency::Counter::kDecoderNoBuffer);
        }
        return;
    }
    int dst_dma_fd = item.value();

    const int64_t transform_start_us = traced ? latency::NowUs() : 0;
    NvBufSurface *src_surface = nullptr;
    NvBufSurface *dst_surface = nullptr;
    int ret = -1;
    if (NvBufSurfaceFromFd(src_dma_fd, (void **)(&src_surface)) == 0 &&
        NvBufSurfaceFromFd(dst_dma_fd, (void **)(&dst_surface)) == 0) {
        ret = NvBufSurfTransform(src_surface, dst_surface, &transform_params_);
    }
    if (traced) {
        latency::RecordSince(latency::Stage::kNvTransform, transform_start_us);
    }
    if (ret < 0) {
        ERROR_PRINT("NvBufSurfTransform failed to transform from fd(%d) to fd(%d)", src_dma_fd,
                    dst_dma_fd);
        free_buffers_.push(dst_dma_fd);
        return;
    }

    auto v4l2_buffer =
        V4L2Buffer::FromCapturedPlane(nullptr, frame_size_, dst_dma_fd, 0, V4L2_PIX_FMT_NV12);
    on_capture(V4L2FrameBuffer::Create(config_.width, config_.height, v4l2_buffer));
    free_buffers_.push(dst_dma_fd);
}

bool JetsonDecoder::CapturePlaneDqCallback(struct v4l2_buffer *v4l2_buf, NvBuffer *buffer,
                                           NvBuffer *shared_buffer, void *arg) {
    JetsonDecoder *thiz = (JetsonDecoder *)arg;

    if (!v4l2_buf || !buffer || buffer->planes[0].bytesused == 0) {
        if (!thiz->abort_) {
            ERROR_PRINT("Decoder capture plane stopped before EOS was requested");
            thiz->abort_ = true;
            thiz->decoder_->abort();
        }
        DEBUG_PRINT("Got EOS, exiting jetson decoder.");
        return false;
    }

    auto item = thiz->capturing_tasks_.pop();
    if (item && !thiz->abort_) {
        thiz->Transform(buffer->planes[0].fd, item.value());
    }

    if (thiz->decoder_->capture_plane.qBuffer(*v4l2_buf, nullptr) < 0) {
        thiz->abort_ = true;
        thiz->decoder_->abort();
        ERROR_PRINT("Failed to enqueue buffer to decoder capture plane");
        return false;
    }

    return true;
}

void JetsonDecoder::SendEOS() {
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[MAX_PLANES];
    NvBuffer *buffer;

    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    memset(planes, 0, sizeof(planes));
    v4l2_buf.m.planes = planes;

    if (decoder_->output_plane.getNumQueuedBuffers() == decoder_->output_plane.getNumBuffers()) {
        if (decoder_->output_plane.dqBuffer(v4l2_buf, &buffer, nullptr, 10) < 0) {
            ERROR_PRINT("Failed to dqBuffer at decoder while sending eos");
        }
    }
    planes[0].bytesused = 0;
    if (decoder_->output_plane.qBuffer(v4l2_buf, nullptr) < 0) {
        ERROR_PRINT("Failed to qBuffer at decoder while sending eos");
    }
}
