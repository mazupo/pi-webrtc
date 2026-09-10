#include "v4l2_capturer.h"

// Linux
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/select.h>

// WebRTC
#include <modules/video_capture/video_capture_factory.h>
#include <third_party/libyuv/include/libyuv.h>

#include "common/latency_tracer.h"
#include "common/logging.h"

std::shared_ptr<V4L2Capturer> V4L2Capturer::Create(Args args) {
    auto ptr = std::make_shared<V4L2Capturer>(args);
    ptr->Initialize();
    ptr->StartCapture();
    return ptr;
}

V4L2Capturer::V4L2Capturer(Args args)
    : camera_id_(args.camera_id),
      fd_(-1),
      fps_(args.fps),
      width_(args.width),
      height_(args.height),
      rotation_(args.rotation),
      buffer_count_(4),
      hw_accel_(args.hw_accel),
      has_first_keyframe_(false),
      format_(args.format),
      config_(args) {}

V4L2Capturer::~V4L2Capturer() {
    worker_.reset();
    decoder_.reset();
    v4l2_util::StreamOff(fd_, capture_.type);
    v4l2_util::DeallocateBuffer(fd_, &capture_);
    v4l2_util::CloseDevice(fd_);
}

void V4L2Capturer::Initialize() {
    if (!hw_accel_ && format_ == V4L2_PIX_FMT_H264) {
        INFO_PRINT("Software decoding H264 camera source is not supported.");
        exit(EXIT_FAILURE);
    }

    std::string devicePath = "/dev/video" + std::to_string(camera_id_);
    fd_ = v4l2_util::OpenDevice(devicePath.c_str());
    if (fd_ < 0) {
        ERROR_PRINT("Unable to open camera device: %s", devicePath.c_str());
        exit(EXIT_FAILURE);
    }

    if (!v4l2_util::InitBuffer(fd_, &capture_, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_MEMORY_MMAP)) {
        ERROR_PRINT("Could not setup v4l2 capture buffer");
        exit(EXIT_FAILURE);
    }

    if (format_ == V4L2_PIX_FMT_H264) {
        if (!SetControls(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)) {
            WARN_PRINT("Unable to set VBR mode");
        }
        if (!SetControls(V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)) {
            WARN_PRINT("Unable to set H264 profile");
        }
        if (!SetControls(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, true)) {
            WARN_PRINT("Unable to set repeat seq header");
        }
        if (!SetControls(V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0)) {
            WARN_PRINT("Unable to set H264 level");
        }
        if (!SetControls(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, 60)) {
            WARN_PRINT("Unable to set H264 I-frame period");
        }
        if (!SetControls(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1)) {
            WARN_PRINT("Unable to force set to key frame");
        }
    }

    if (!v4l2_util::SetFps(fd_, capture_.type, fps_)) {
        WARN_PRINT("Unable to set fps");
    }

    if (rotation_ > 0 && !v4l2_util::SetCtrl(fd_, V4L2_CID_ROTATE, rotation_)) {
        WARN_PRINT("Unable to set the rotation angle");
    }

    if (!v4l2_util::SetFormat(fd_, &capture_, width_, height_, format_)) {
        ERROR_PRINT("Unable to set the resolution: %dx%d", width_, height_);
    }

    if (format_ == V4L2_PIX_FMT_H264 &&
        !SetControls(V4L2_CID_MPEG_VIDEO_BITRATE, 10 * 1024 * 1024)) {
        WARN_PRINT("Unable to set video bitrate");
    }
}

int V4L2Capturer::fps() const { return fps_; }

int V4L2Capturer::width(int stream_idx) const { return width_; }

int V4L2Capturer::height(int stream_idx) const { return height_; }

bool V4L2Capturer::is_dma_capture() const { return hw_accel_ && IsCompressedFormat(); }

uint32_t V4L2Capturer::format() const { return format_; }

Args V4L2Capturer::config() const { return config_; }

bool V4L2Capturer::IsCompressedFormat() const {
    return format_ == V4L2_PIX_FMT_MJPEG || format_ == V4L2_PIX_FMT_H264;
}

bool V4L2Capturer::CheckMatchingDevice(std::string unique_name) {
    struct v4l2_capability cap;
    if (v4l2_util::QueryCapabilities(fd_, &cap) && cap.bus_info[0] != 0 &&
        strcmp((const char *)cap.bus_info, unique_name.c_str()) == 0) {
        return true;
    }
    return false;
}

int V4L2Capturer::GetCameraIndex(webrtc::VideoCaptureModule::DeviceInfo *device_info) {
    for (int i = 0; i < device_info->NumberOfDevices(); i++) {
        char device_name[256];
        char unique_name[256];
        if (device_info->GetDeviceName(static_cast<uint32_t>(i), device_name, sizeof(device_name),
                                       unique_name, sizeof(unique_name)) == 0 &&
            CheckMatchingDevice(unique_name)) {
            DEBUG_PRINT("GetDeviceName(%d): device_name=%s, unique_name=%s", i, device_name,
                        unique_name);
            return i;
        }
    }
    return -1;
}

void V4L2Capturer::CaptureImage() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    int r = select(fd_ + 1, &fds, NULL, NULL, &tv);
    if (r == -1) {
        ERROR_PRINT("select failed");
        return;
    } else if (r == 0) { // timeout
        DEBUG_PRINT("capture timeout");
        return;
    }

    v4l2_buffer buf = {};
    buf.type = capture_.type;
    buf.memory = capture_.memory;

    if (!v4l2_util::DequeueBuffer(fd_, &buf)) {
        return;
    }

    auto buffer = V4L2Buffer::FromV4L2((uint8_t *)capture_.buffers[buf.index].start, buf, format_);
    frame_buffer_ = V4L2FrameBuffer::Create(width_, height_, buffer);

    if (latency::Enabled()) {
        latency::RecordCapture(latency::SensorUs(buffer.timestamp), latency::NowUs());
    }
    if (hw_accel_ && format_ == V4L2_PIX_FMT_H264) {
        if ((buffer.flags & V4L2_BUF_FLAG_KEYFRAME) != 0) {
            has_first_keyframe_ = true;
        }
        if (!has_first_keyframe_) {
            v4l2_util::QueueBuffer(fd_, &buf);
            return;
        }
    }

    if (hw_accel_ && IsCompressedFormat()) {
        if (!decoder_) {
#if defined(USE_RPI_HW_ENCODER)
            decoder_ = V4L2Decoder::Create({width_, height_, format_, true});
#elif defined(USE_JETSON_HW_ENCODER)
            decoder_ = JetsonDecoder::Create({width_, height_, format_, true});
#endif
            if (!decoder_) {
                ERROR_PRINT("Unable to create the hardware decoder for %s",
                            v4l2_util::FourccToString(format_).c_str());
                exit(EXIT_FAILURE);
            }
        }

        decoder_->EmplaceBuffer(frame_buffer_, [this, buffer](V4L2FrameBufferRef decoded_buffer) {
            // hw decoder doesn't output timestamps.
            decoded_buffer->SetTimestamp(buffer.timestamp);
            stream_subject_.Next(decoded_buffer);
        });
    } else {
        stream_subject_.Next(frame_buffer_);
    }

    if (!v4l2_util::QueueBuffer(fd_, &buf)) {
        return;
    }
}

bool V4L2Capturer::SetControls(int key, int value) {
    return v4l2_util::SetExtCtrl(fd_, key, value);
}

webrtc::scoped_refptr<webrtc::I420BufferInterface> V4L2Capturer::GetI420Frame(int stream_idx) {
    return frame_buffer_->ToI420();
}

Subscription V4L2Capturer::Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                     int stream_idx) {
    return stream_subject_.Subscribe(std::move(callback));
}

void V4L2Capturer::StartCapture() {
    if (!v4l2_util::AllocateBuffer(fd_, &capture_, buffer_count_) ||
        !v4l2_util::QueueBuffers(fd_, &capture_)) {
        exit(0);
    }

    v4l2_util::StreamOn(fd_, capture_.type);

    worker_ = std::make_unique<Worker>("V4L2 Capturer", [this]() {
        CaptureImage();
    });
    worker_->Run();
}
