#include "codecs/jetson/jetson_encoder.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_utils.h"
#include <cstring>

#include "Error.h"
#include "NvBuffer.h"

const int BUFFER_NUM = 4;

static std::atomic<uint32_t> global_enc_id{0};

std::unique_ptr<JetsonEncoder> JetsonEncoder::Create(EncoderConfig config) {
    char auto_name[16];
    snprintf(auto_name, sizeof(auto_name), "enc%d", global_enc_id.fetch_add(1) % 10);
    auto ptr = std::make_unique<JetsonEncoder>(config, auto_name);
    if (!ptr->Initialize()) {
        return nullptr;
    }
    ptr->Start();
    return ptr;
}

JetsonEncoder::JetsonEncoder(EncoderConfig config, const char *name)
    : abort_(true),
      encoder_(nullptr),
      name_(name),
      config_(config) {}

JetsonEncoder::~JetsonEncoder() {
    abort_ = true;

    SendEOS();

    /* Wait till capture plane DQ Thread finishes
       i.e. all the capture plane buffers are dequeued */
    encoder_->capture_plane.waitForDQThread(-1);
    encoder_->capture_plane.deinitPlane();
    encoder_->output_plane.deinitPlane();

    if (encoder_) {
        delete encoder_;
        encoder_ = nullptr;
    }

    DEBUG_PRINT("~JetsonEncoder");
}

bool JetsonEncoder::CreateVideoEncoder() {
    int ret = 0;

    encoder_ = NvVideoEncoder::createVideoEncoder(name_);
    if (!encoder_)
        ORIGINATE_ERROR("Could not create encoder");

    ret = encoder_->setCapturePlaneFormat(config_.dst_pix_fmt, config_.width, config_.height,
                                          2 * 1024 * 1024);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set capture plane format");

    if (config_.dst_pix_fmt == V4L2_PIX_FMT_AV1) {
        ret = DisableAV1IVF();
        if (ret < 0)
            ORIGINATE_ERROR("Could not disable IVF headers for AV1 codec");
    }

    ret = encoder_->setOutputPlaneFormat(V4L2_PIX_FMT_NV12M, config_.width, config_.height);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set output plane format");

    ret = encoder_->setBitrate(config_.bitrate);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set bitrate");

    if (config_.dst_pix_fmt == V4L2_PIX_FMT_H264) {
        ret = encoder_->setProfile(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
        ret = encoder_->setLevel(V4L2_MPEG_VIDEO_H264_LEVEL_5_1); // 4k60fps needs level 5.2
        if (ret < 0)
            ORIGINATE_ERROR("Could not set encoder level");

        ret = encoder_->setNumBFrames(0);
        if (ret < 0)
            ORIGINATE_ERROR("Could not set B frame number");

        ret = encoder_->setInsertVuiEnabled(true);
        if (ret < 0)
            ORIGINATE_ERROR("Could not insert Video Usability Information");
    }

    ret = encoder_->setInsertSpsPpsAtIdrEnabled(true);
    if (ret < 0)
        ORIGINATE_ERROR("Could not insert SPS PPS at every IDR");

    ret = encoder_->setRateControlMode(config_.rc_mode);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set rate control mode");

    ret = encoder_->setIDRInterval(config_.idr_interval);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set IDR interval");

    ret = encoder_->setIFrameInterval(config_.keyframe_interval);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set I-frame interval");

    ret = encoder_->setFrameRate(config_.fps, 1);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set encoder framerate");

    ret = encoder_->setHWPresetType(V4L2_ENC_HW_PRESET_ULTRAFAST);
    if (ret < 0)
        ORIGINATE_ERROR("Could not set encoder HW Preset");

    /* Query, Export and Map the output plane buffers so that we can read
       raw data into the buffers */
    if (config_.is_dma_src) {
        INFO_PRINT("Set output dma buffer parameters");
        ret = encoder_->output_plane.reqbufs(V4L2_MEMORY_DMABUF, BUFFER_NUM);
        if (ret)
            ORIGINATE_ERROR("reqbufs failed for output plane V4L2_MEMORY_DMABUF");
    } else {
        INFO_PRINT("Set output mmap parameters");
        ret = encoder_->output_plane.setupPlane(V4L2_MEMORY_MMAP, BUFFER_NUM, true, false);
        if (ret < 0)
            ORIGINATE_ERROR("Could not setup output plane");
    }

    /* Query, Export and Map the output plane buffers so that we can write
       encoder_oded data from the buffers */
    ret = encoder_->capture_plane.setupPlane(V4L2_MEMORY_MMAP, BUFFER_NUM, true, false);
    if (ret < 0)
        ORIGINATE_ERROR("Could not setup capture plane");

    return true;
}

bool JetsonEncoder::PrepareCaptureBuffer() {
    /* Enqueue all the empty capture plane buffers */
    for (uint32_t i = 0; i < encoder_->capture_plane.getNumBuffers(); i++) {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;

        if (encoder_->capture_plane.qBuffer(v4l2_buf, NULL) < 0)
            ORIGINATE_ERROR("Failed to queue buffers into encoder capture plane");
    }

    return true;
}

void JetsonEncoder::SetFps(int adjusted_fps) {
    if (config_.fps != adjusted_fps) {
        config_.fps = adjusted_fps;
        int ret = encoder_->setFrameRate(config_.fps, 1);
        if (ret < 0)
            ERROR_PRINT("Could not set encoder framerate to %d", config_.fps);
    }
}

void JetsonEncoder::SetBitrate(int adjusted_bitrate_bps) {
    if (config_.bitrate == adjusted_bitrate_bps) {
        return;
    }
    config_.bitrate = adjusted_bitrate_bps;

    v4l2_ext_control control{};
    control.id = V4L2_CID_MPEG_VIDEO_BITRATE;
    control.value = adjusted_bitrate_bps;

    v4l2_ext_controls ctrls{};
    ctrls.count = 1;
    ctrls.controls = &control;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;

    if (encoder_->setExtControls(ctrls) < 0) {
        ERROR_PRINT("Could not set encoder bitrate to %d", adjusted_bitrate_bps);
    }
}

void JetsonEncoder::ForceKeyFrame() {
    int ret = encoder_->forceIDR();
    if (ret < 0)
        ERROR_PRINT("Could not force set encoder to key frame");
}

bool JetsonEncoder::Initialize() {
    if (!CreateVideoEncoder()) {
        ERROR_PRINT("Failed to create video encoder");
        return false;
    }
    return true;
}

void JetsonEncoder::Start() {
    /* Stream on */
    int e = encoder_->output_plane.setStreamStatus(true);
    if (e < 0)
        ERROR_PRINT("Failed to stream on output plane");
    e = encoder_->capture_plane.setStreamStatus(true);
    if (e < 0)
        ERROR_PRINT("Failed to stream on capture plane");

    /* Set video encoder callback */
    encoder_->capture_plane.setDQThreadCallback(EncoderCapturePlaneDqCallback);

    /* startDQThread starts a thread internally which calls the
       encoderCapturePlaneDqCallback whenever a buffer is dequeued
       on the plane */
    encoder_->capture_plane.startDQThread(this);

    PrepareCaptureBuffer();

    abort_ = false;
}

void JetsonEncoder::EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                                  std::function<void(V4L2FrameBufferRef)> on_capture) {
    if (encoder_->isInError()) {
        ERROR_PRINT("ERROR in encoder");
        return;
    }

    if (abort_) {
        return;
    }

    struct v4l2_buffer v4l2_output_buf;
    struct v4l2_plane output_planes[MAX_PLANES];
    NvBuffer *outplane_buffer = NULL;

    memset(&v4l2_output_buf, 0, sizeof(v4l2_output_buf));
    memset(output_planes, 0, sizeof(output_planes));
    v4l2_output_buf.m.planes = output_planes;

    NvBuffer *nv_buffer;

    if (encoder_->output_plane.getNumQueuedBuffers() == encoder_->output_plane.getNumBuffers()) {
        if (encoder_->output_plane.dqBuffer(v4l2_output_buf, &nv_buffer, NULL, 10) < 0) {
            if (latency::Enabled()) {
                latency::Count(latency::Counter::kEncoderDqTimeout);
            }
            ERROR_PRINT("Failed to dqBuffer at encoder output_plane");
            return;
        }
    } else {
        nv_buffer =
            encoder_->output_plane.getNthBuffer(encoder_->output_plane.getNumQueuedBuffers());
        v4l2_output_buf.index = nv_buffer->index;
    }

    if (config_.is_dma_src) {
        v4l2_output_buf.m.planes[0].m.fd = frame_buffer->GetDmaFd();
        v4l2_output_buf.m.planes[0].bytesused = 1; // byteused must be non-zero
    } else {
        ConvertI420ToYUV420M(nv_buffer, frame_buffer->ToI420());
    }

    if (encoder_->output_plane.qBuffer(v4l2_output_buf, nullptr) < 0) {
        ERROR_PRINT("Failed to qBuffer at encoder output_plane");
        return;
    }

    if (latency::Enabled()) {
        const int64_t queued_us = latency::NowUs();
        capturing_tasks_.push([on_capture, queued_us](V4L2FrameBufferRef encoded_buffer) {
            latency::RecordSince(latency::Stage::kHwEncodeDwell, queued_us);
            on_capture(encoded_buffer);
        });
        return;
    }

    capturing_tasks_.push(on_capture);
}

bool JetsonEncoder::EncoderCapturePlaneDqCallback(struct v4l2_buffer *v4l2_buf, NvBuffer *buffer,
                                                  NvBuffer *shared_buffer, void *arg) {
    JetsonEncoder *thiz = (JetsonEncoder *)arg;

    if (!v4l2_buf) {
        thiz->abort_ = true;
        thiz->encoder_->abort();
        ERROR_PRINT("Failed to dequeue buffer from encoder capture plane");
        return false;
    }

    auto item = thiz->capturing_tasks_.pop();
    if (item) {
        auto v4l2buffer = V4L2Buffer::FromCapturedPlane(
            buffer->planes[0].data, buffer->planes[0].bytesused, buffer->planes[0].fd,
            v4l2_buf->flags, thiz->config_.dst_pix_fmt);
        auto encoded_frame_buffer =
            V4L2FrameBuffer::Create(thiz->config_.width, thiz->config_.height, v4l2buffer);
        auto task = item.value();
        task(encoded_frame_buffer);
    }

    if (thiz->encoder_->capture_plane.qBuffer(*v4l2_buf, NULL) < 0) {
        thiz->abort_ = true;
        thiz->encoder_->abort();
        ERROR_PRINT("Failed to enqueue buffer to encoder capture plane");
        return false;
    }

    /* GOT EOS from encoder. Stop dqthread */
    if (buffer->planes[0].bytesused == 0) {
        DEBUG_PRINT("Got EOS, exiting jetson encoder.");
        return false;
    }

    return true;
}

void JetsonEncoder::ConvertI420ToYUV420M(
    NvBuffer *nv_buffer, webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer) {
    for (uint32_t p = 0; p < nv_buffer->n_planes; p++) {
        const uint8_t *src_addr;
        int stride;
        if (p == 0) {
            src_addr = i420_buffer->DataY();
            stride = i420_buffer->StrideY();
        } else if (p == 1) {
            src_addr = i420_buffer->DataU();
            stride = i420_buffer->StrideU();
        } else if (p == 2) {
            src_addr = i420_buffer->DataV();
            stride = i420_buffer->StrideV();
        } else {
            break;
        }
        auto &plane = nv_buffer->planes[p];
        int row_size = plane.fmt.bytesperpixel * plane.fmt.width;
        uint8_t *dst_addr = plane.data;
        plane.bytesused = 0;
        for (uint32_t row = 0; row < plane.fmt.height; row++) {
            memcpy(dst_addr, src_addr + stride * row, row_size);
            dst_addr += plane.fmt.stride;
        }
        plane.bytesused = plane.fmt.stride * plane.fmt.height;
    }
}

// Send End of Stream to encoder by queueing on the output plane a buffer with bytesused = 0 for
// the 0th plane (v4l2_buffer.m.planes[0].bytesused = 0).
void JetsonEncoder::SendEOS() {
    struct v4l2_buffer v4l2_buffer;
    struct v4l2_plane planes[MAX_PLANES];
    NvBuffer *buffer;

    memset(&v4l2_buffer, 0, sizeof(v4l2_buffer));
    memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));
    v4l2_buffer.m.planes = planes;

    if (encoder_->output_plane.getNumQueuedBuffers() == encoder_->output_plane.getNumBuffers()) {
        if (encoder_->output_plane.dqBuffer(v4l2_buffer, &buffer, NULL, 10) < 0) {
            ERROR_PRINT("Failed to dqBuffer at encoder while sending eos");
        }
    }
    planes[0].bytesused = 0;
    if (encoder_->output_plane.qBuffer(v4l2_buffer, NULL) < 0) {
        ERROR_PRINT("Failed to qBuffer at encoder while sending eos");
    }
}

uint32_t JetsonEncoder::DisableAV1IVF() {
    struct v4l2_ext_control control;
    struct v4l2_ext_controls ctrls;

    memset(&control, 0, sizeof(control));
    memset(&ctrls, 0, sizeof(ctrls));

    control.id = V4L2_CID_MPEG_VIDEOENC_AV1_HEADERS_WITH_FRAME;
    control.value = 0;

    ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    ctrls.count = 1;
    ctrls.controls = &control;

    return encoder_->setExtControls(ctrls);
}
