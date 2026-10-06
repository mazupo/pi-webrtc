#include "codecs/v4l2/v4l2_codec.h"
#include "common/latency_tracer.h"
#include "common/logging.h"
#include <algorithm>
#include <cstring>
#include <sys/ioctl.h>
#include <thread>

V4L2Codec::V4L2Codec()
    : fd_(-1),
      width_(0),
      height_(0),
      dst_fmt_(0),
      abort_(false) {}

V4L2Codec::~V4L2Codec() {
    abort_ = true;
    worker_.reset();

    if (fd_ < 0) {
        return;
    }

    v4l2_util::StreamOff(fd_, output_.type);
    v4l2_util::StreamOff(fd_, capture_.type);

    v4l2_util::DeallocateBuffer(fd_, &output_);
    v4l2_util::DeallocateBuffer(fd_, &capture_);

    v4l2_util::CloseDevice(fd_);
}

bool V4L2Codec::Open(const char *file_name) {
    file_name_ = file_name;
    fd_ = v4l2_util::OpenDevice(file_name);
    if (fd_ < 0) {
        return false;
    }
    return true;
}

bool V4L2Codec::SetFps(uint32_t fps) { return v4l2_util::SetFps(fd_, output_.type, fps); }

bool V4L2Codec::SetExtCtrl(uint32_t id, int32_t value) {
    return v4l2_util::SetExtCtrl(fd_, id, value);
}

bool V4L2Codec::SetupOutputBuffer(int width, int height, uint32_t pix_fmt, v4l2_memory memory,
                                  int buffer_num, uint32_t sizeimage) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    return PrepareBuffer(&output_, width, height, pix_fmt, type, memory, buffer_num, false,
                         sizeimage);
}

bool V4L2Codec::SetupCaptureBuffer(int width, int height, uint32_t pix_fmt, v4l2_memory memory,
                                   int buffer_num, bool exp_dmafd, bool allow_padding) {
    width_ = width;
    height_ = height;
    dst_fmt_ = pix_fmt;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    return PrepareBuffer(&capture_, width, height, pix_fmt, type, memory, buffer_num, exp_dmafd, 0,
                         allow_padding);
}

bool V4L2Codec::SetOutputCrop(int width, int height) {
    return v4l2_util::SetCrop(fd_, output_.type, width, height);
}

bool V4L2Codec::PrepareBuffer(V4L2BufferGroup *gbuffer, int width, int height, uint32_t pix_fmt,
                              v4l2_buf_type type, v4l2_memory memory, int buffer_num,
                              bool has_dmafd, uint32_t sizeimage, bool allow_padding) {
    if (!v4l2_util::InitBuffer(fd_, gbuffer, type, memory, has_dmafd)) {
        return false;
    }

    if (!v4l2_util::SetFormat(fd_, gbuffer, width, height, pix_fmt, sizeimage, allow_padding)) {
        return false;
    }

    if (!v4l2_util::AllocateBuffer(fd_, gbuffer, buffer_num)) {
        return false;
    }

    if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        for (int i = 0; i < buffer_num; i++) {
            output_buffer_index_.push(i);
        }
    } else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        if (!v4l2_util::QueueBuffers(fd_, gbuffer)) {
            return false;
        }
    }

    return true;
}

bool V4L2Codec::SubscribeEvent(uint32_t ev_type) { return v4l2_util::SubscribeEvent(fd_, ev_type); }

void V4L2Codec::HandleEvent() {
    struct v4l2_event ev;
    while (!ioctl(fd_, VIDIOC_DQEVENT, &ev)) {
        switch (ev.type) {
            case V4L2_EVENT_SOURCE_CHANGE:
                DEBUG_PRINT("Source changed!");
                v4l2_util::StreamOff(fd_, capture_.type);
                v4l2_util::DeallocateBuffer(fd_, &capture_);
                v4l2_util::SetFormat(fd_, &capture_, 0, 0, dst_fmt_);
                v4l2_util::AllocateBuffer(fd_, &capture_, capture_.buffers.size());
                v4l2_util::StreamOn(fd_, capture_.type);
                break;
            case V4L2_EVENT_EOS:
                DEBUG_PRINT("EOS!");
                exit(EXIT_FAILURE);
                break;
        }
    }
}

void V4L2Codec::Start() {
    if (output_.type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        ERROR_PRINT("Output buffer is not set for device: %s", file_name_);
        exit(EXIT_FAILURE);
    }
    if (capture_.type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        ERROR_PRINT("Capture buffer is not set for device: %s", file_name_);
        exit(EXIT_FAILURE);
    }

    v4l2_util::StreamOn(fd_, output_.type);
    v4l2_util::StreamOn(fd_, capture_.type);

    abort_ = false;
    worker_ = std::make_unique<Worker>(file_name_, [this]() {
        CaptureBuffer();
    });
    worker_->Run();
}

void V4L2Codec::EmplaceBuffer(V4L2FrameBufferRef buffer,
                              std::function<void(V4L2FrameBufferRef)> on_capture) {
    auto item =
        input_wait_ms_ > 0 ? output_buffer_index_.pop(input_wait_ms_) : output_buffer_index_.pop();
    if (!item) {
        if (latency::Enabled()) {
            latency::Count(latency::Counter::kV4L2NoBuffer);
        }
        return;
    }
    auto index = item.value();

    v4l2_buffer *buf = &output_.buffers[index].inner;
    if (output_.memory == V4L2_MEMORY_DMABUF) {
        // An odd-height frame's payload is a few bytes short of sizeimage, which vb2 needs as the
        // plane length; the dmabuf itself is page-aligned and holds it.
        buf->m.planes[0].m.fd = buffer->GetDmaFd();
        buf->m.planes[0].length = std::max(buffer->size(), output_.sizeimage);
    } else {
        if (buffer->size() > output_.buffers[index].length) {
            ERROR_PRINT("Frame (%u bytes) exceeds the input buffer (%u bytes) of %s",
                        buffer->size(), output_.buffers[index].length, file_name_);
            output_buffer_index_.push(index);
            return;
        }
        memcpy((uint8_t *)output_.buffers[index].start, (uint8_t *)buffer->Data(), buffer->size());
    }
    buf->m.planes[0].bytesused = buffer->size();

    if (!v4l2_util::QueueBuffer(fd_, buf)) {
        ERROR_PRINT("QueueBuffer V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE. fd(%d) at index %d", fd_,
                    index);
        output_buffer_index_.push(index);
        return;
    }

    if (latency::Enabled()) {
        const int64_t queued_us = latency::NowUs();
        const latency::Stage stage = dwell_stage_;
        capturing_tasks_.push([on_capture, queued_us, stage](V4L2FrameBufferRef encoded_buffer) {
            latency::RecordSince(stage, queued_us);
            on_capture(encoded_buffer);
        });
        return;
    }

    capturing_tasks_.push(on_capture);
}

bool V4L2Codec::CaptureBuffer() {

    if (abort_) {
        return false;
    }

    fd_set rd_fds; /* capture done */
    fd_set wr_fds; /* output done */
    fd_set ex_fds; /* events */
    FD_ZERO(&rd_fds);
    FD_SET(fd_, &rd_fds);
    FD_ZERO(&wr_fds);
    FD_SET(fd_, &wr_fds);
    FD_ZERO(&ex_fds);
    FD_SET(fd_, &ex_fds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;

    int r = select(fd_ + 1, &rd_fds, &wr_fds, &ex_fds, &tv);

    if (abort_) {
        return false;
    } else if (r <= 0) { // failed or timeout
        return false;
    }

    // A decoder holds several inputs before its first output, so free each side as it completes.
    if (FD_ISSET(fd_, &wr_fds)) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes = {};
        buf.memory = output_.memory;
        buf.length = 1;
        buf.m.planes = &planes;
        buf.type = output_.type;
        if (v4l2_util::DequeueBuffer(fd_, &buf)) {
            output_buffer_index_.push(buf.index);
        }
    }

    if (FD_ISSET(fd_, &rd_fds)) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes = {};
        buf.memory = capture_.memory;
        buf.length = 1;
        buf.m.planes = &planes;
        buf.type = capture_.type;
        if (!v4l2_util::DequeueBuffer(fd_, &buf)) {
            return false;
        }

        auto buffer = V4L2Buffer::FromCapturedPlane(
            capture_.buffers[buf.index].start, buf.m.planes[0].bytesused,
            capture_.buffers[buf.index].dmafd, buf.flags, dst_fmt_);
        auto frame_buffer = V4L2FrameBuffer::Create(width_, height_, buffer);
        if (capture_.bytesperline > 0) {
            frame_buffer->SetLayout(capture_.bytesperline, capture_.height);
        }

        if (abort_) {
            return false;
        }

        auto item = capturing_tasks_.pop();
        if (item) {
            auto task = item.value();
            task(frame_buffer);
        }

        if (!v4l2_util::QueueBuffer(fd_, &capture_.buffers[buf.index].inner)) {
            return false;
        }
    }

    if (FD_ISSET(fd_, &ex_fds)) {
        ERROR_PRINT("Exception in fd(%d).", fd_);
        HandleEvent();
    }

    return true;
}
