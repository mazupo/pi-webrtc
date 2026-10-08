#include "libcamera_capturer.h"

#include <algorithm>
#include <sys/mman.h>

#include "common/latency_tracer.h"
#include "common/logging.h"
#include "common/v4l2_utils.h"
#include <libcamera/geometry.h>

std::shared_ptr<LibcameraCapturer> LibcameraCapturer::Create(Args args) {
    auto ptr = std::make_shared<LibcameraCapturer>(args);
    ptr->InitCamera();
    ptr->InitControls(args);
    ptr->StartCapture();
    return ptr;
}

LibcameraCapturer::LibcameraCapturer(Args args)
    : camera_id_(args.camera_id),
      num_streams_(std::clamp(args.num_streams, 1, kMaxStreams)),
      fps_(args.fps),
      rotation_(args.rotation),
      buffer_count_(2),
      format_(args.format),
      config_(args),
      is_controls_updated_(false) {
    streams_[0].width = args.width;
    streams_[0].height = args.height;
    streams_[1].width = args.sub_width;
    streams_[1].height = args.sub_height;
}

LibcameraCapturer::~LibcameraCapturer() {
    camera_->stop();
    for (int i = 0; i < num_streams_; i++) {
        allocator_->free(streams_[i].stream);
    }
    allocator_.reset();
    camera_config_.reset();
    camera_->release();
    camera_.reset();
    cm_->stop();
}

void LibcameraCapturer::InitCamera() {
    cm_ = std::make_unique<libcamera::CameraManager>();
    int ret = cm_->start();
    if (ret) {
        throw std::runtime_error("Failed to start camera manager");
    }

    auto cameras = cm_->cameras();
    if (cameras.size() == 0) {
        throw std::runtime_error("No camera is available via libcamera.");
    }

    if (camera_id_ >= cameras.size()) {
        throw std::runtime_error("Selected camera is not available.");
    }

    std::string const &cam_id = cameras[camera_id_]->id();
    INFO_PRINT("camera id: %s", cam_id.c_str());
    camera_ = cm_->get(cam_id);
    camera_->acquire();

    std::vector<libcamera::StreamRole> roles = {libcamera::StreamRole::VideoRecording};
    if (num_streams_ > 1) {
        // The ISP scales the sub-stream from the same frame, as rpicam-apps does for "lores".
        roles.push_back(libcamera::StreamRole::Viewfinder);
    }
    camera_config_ = camera_->generateConfiguration(roles);
    if (!camera_config_ || camera_config_->size() != roles.size()) {
        ERROR_PRINT("The camera cannot output %zu streams at once.", roles.size());
        exit(EXIT_FAILURE);
    }

    if (rotation_ == 90) {
        camera_config_->orientation = libcamera::Orientation::Rotate90;
    } else if (rotation_ == 180) {
        camera_config_->orientation = libcamera::Orientation::Rotate180;
    } else if (rotation_ == 270) {
        camera_config_->orientation = libcamera::Orientation::Rotate270;
    }

    controls_ = libcamera::ControlList(camera_->controls());
    int64_t frame_time = 1000000 / fps_;
    controls_.set(libcamera::controls::FrameDurationLimits,
                  libcamera::Span<const int64_t, 2>({frame_time, frame_time}));

    // Both streams come out of one ISP pass, so they share the colour space of the main one.
    const auto color_space =
        streams_[0].width >= 720 ? libcamera::ColorSpace::Rec709 : libcamera::ColorSpace::Smpte170m;
    for (int i = 0; i < num_streams_; i++) {
        auto &stream_config = camera_config_->at(i);
        DEBUG_PRINT("camera original format %d: %s", i, stream_config.toString().c_str());
        if (streams_[i].width && streams_[i].height) {
            stream_config.size = libcamera::Size(streams_[i].width, streams_[i].height);
        }
        stream_config.pixelFormat = libcamera::formats::YUV420;
        stream_config.bufferCount = buffer_count_;
        stream_config.colorSpace = color_space;
    }

    auto validation = camera_config_->validate();
    if (validation == libcamera::CameraConfiguration::Status::Invalid) {
        ERROR_PRINT("Failed to validate camera configuration.");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_streams_; i++) {
        const auto &stream_config = camera_config_->at(i);
        const char *name = i == 0 ? "main" : "sub";
        INFO_PRINT("camera %s %s stream: %s.",
                   validation == libcamera::CameraConfiguration::Status::Adjusted ? "adjusted"
                                                                                  : "validated",
                   name, stream_config.toString().c_str());

        streams_[i].width = stream_config.size.width;
        streams_[i].height = stream_config.size.height;
        INFO_PRINT("  width: %d, height: %d, stride: %d", streams_[i].width, streams_[i].height,
                   stream_config.stride);

        // Frames are read as tightly packed I420, which a padded row would break.
        if (static_cast<unsigned int>(streams_[i].width) != stream_config.stride) {
            ERROR_PRINT("The %s stream's stride (%u) is not equal to its width (%d). Try a width "
                        "that is a multiple of 64.",
                        name, stream_config.stride, streams_[i].width);
            exit(EXIT_FAILURE);
        }
    }
}

void LibcameraCapturer::InitControls(Args args) {
    if (!controls_.get(libcamera::controls::AnalogueGain) && args.gain) {
        controls_.set(libcamera::controls::AnalogueGainMode,
                      libcamera::controls::AnalogueGainModeManual);
        controls_.set(libcamera::controls::AnalogueGain, args.gain);
    }

    if (!controls_.get(libcamera::controls::Sharpness)) {
        controls_.set(libcamera::controls::Sharpness, args.sharpness);
    }
    if (!controls_.get(libcamera::controls::Contrast)) {
        controls_.set(libcamera::controls::Contrast, args.contrast);
    }
    if (!controls_.get(libcamera::controls::Brightness)) {
        controls_.set(libcamera::controls::Brightness, args.brightness);
    }
    if (!controls_.get(libcamera::controls::Saturation)) {
        controls_.set(libcamera::controls::Saturation, args.saturation);
    }
    if (!controls_.get(libcamera::controls::ExposureValue)) {
        controls_.set(libcamera::controls::ExposureValue, args.ev);
    }

    if (controls_.get(libcamera::controls::ExposureTime) && args.shutter) {
        controls_.set(libcamera::controls::ExposureTimeMode,
                      libcamera::controls::ExposureTimeModeManual);
        controls_.set(libcamera::controls::ExposureTime,
                      args.shutter.get<std::chrono::microseconds>());
    }

    if (!controls_.get(libcamera::controls::AeMeteringMode)) {
        controls_.set(libcamera::controls::AeMeteringMode, args.ae_metering_mode);
    }
    if (!controls_.get(libcamera::controls::AeExposureMode)) {
        controls_.set(libcamera::controls::AeExposureMode, args.ae_mode);
    }
    if (!controls_.get(libcamera::controls::AwbMode)) {
        controls_.set(libcamera::controls::AwbMode, args.awb_mode);
    }

    if (!controls_.get(libcamera::controls::draft::NoiseReductionMode)) {
        controls_.set(libcamera::controls::draft::NoiseReductionMode, args.denoise_mode);
    }

    if (!controls_.get(libcamera::controls::ColourGains)) {
        controls_.set(libcamera::controls::ColourGains,
                      libcamera::Span<const float, 2>({args.awb_gain_r, args.awb_gain_b}));
    }

    if (!controls_.get(libcamera::controls::AfMode) &&
        camera_->controls().count(&libcamera::controls::AfMode) > 0) {
        if (args.af_mode == -1) {
            if (args.lens_position || args.set_default_lens_position) {
                args.af_mode = libcamera::controls::AfModeManual;
            } else {
                if (camera_->controls().find(libcamera::controls::AF_MODE) !=
                        camera_->controls().end() &&
                    camera_->controls().count(&libcamera::controls::AfMode) > 0) {
                    args.af_mode =
                        camera_->controls().at(&libcamera::controls::AfMode).max().get<int>();
                }
            }
        }
        controls_.set(libcamera::controls::AfMode, args.af_mode);
    }
    if (!controls_.get(libcamera::controls::AfRange) &&
        camera_->controls().count(&libcamera::controls::AfRange) > 0) {
        controls_.set(libcamera::controls::AF_RANGE, args.af_range_mode);
    }
    if (!controls_.get(libcamera::controls::AfSpeed) &&
        camera_->controls().count(&libcamera::controls::AfSpeed) > 0) {

        controls_.set(libcamera::controls::AF_SPEED, args.af_speed_mode);
    }

    if (!controls_.get(libcamera::controls::AfWindows) &&
        !controls_.get(libcamera::controls::AfMetering) && args.af_window_width != 0 &&
        args.af_window_height != 0) {
        libcamera::Rectangle sensor_area = camera_->controls()
                                               .at(&libcamera::controls::ScalerCrop)
                                               .max()
                                               .get<libcamera::Rectangle>();
        int x = args.af_window_x * sensor_area.width;
        int y = args.af_window_y * sensor_area.height;
        int w = args.af_window_width * sensor_area.width;
        int h = args.af_window_height * sensor_area.height;
        libcamera::Rectangle afwindows_rectangle[1];
        afwindows_rectangle[0] = libcamera::Rectangle(x, y, w, h);
        afwindows_rectangle[0].translateBy(sensor_area.topLeft());

        controls_.set(libcamera::controls::AfMetering, libcamera::controls::AfMeteringWindows);
        controls_.set(libcamera::controls::AfWindows,
                      libcamera::Span<const libcamera::Rectangle>(afwindows_rectangle));
    }

    if (!controls_.get(libcamera::controls::AfTrigger) &&
        args.af_mode == libcamera::controls::AfModeEnum::AfModeAuto) {
        controls_.set(libcamera::controls::AfTrigger, libcamera::controls::AfTriggerStart);
    } else if (!controls_.get(libcamera::controls::LensPosition) &&
               camera_->controls().count(&libcamera::controls::LensPosition) > 0 &&
               (args.lens_position || args.set_default_lens_position)) {
        float f;
        if (args.lens_position) {
            f = args.lens_position.value();
        } else {
            f = camera_->controls().at(&libcamera::controls::LensPosition).def().get<float>();
        }
        controls_.set(libcamera::controls::LensPosition, f);
    }
}

int LibcameraCapturer::fps() const { return fps_; }

const LibcameraCapturer::StreamState &LibcameraCapturer::StreamAt(int stream_idx) const {
    return streams_[stream_idx > 0 && stream_idx < num_streams_ ? stream_idx : 0];
}

LibcameraCapturer::StreamState &LibcameraCapturer::StreamAt(int stream_idx) {
    return streams_[stream_idx > 0 && stream_idx < num_streams_ ? stream_idx : 0];
}

int LibcameraCapturer::width(int stream_idx) const { return StreamAt(stream_idx).width; }

int LibcameraCapturer::height(int stream_idx) const { return StreamAt(stream_idx).height; }

bool LibcameraCapturer::has_sub_stream() const { return num_streams_ > 1; }

bool LibcameraCapturer::is_dma_capture() const { return true; }

uint32_t LibcameraCapturer::format() const { return format_; }

Args LibcameraCapturer::config() const { return config_; }

bool LibcameraCapturer::SetControls(int key, int value) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    DEBUG_PRINT("  Set camera control: %d, %d", key, value);
    controls_.set(key, value);
    is_controls_updated_ = true;
    return true;
}

void LibcameraCapturer::AllocateBuffer() {
    allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);

    for (int i = 0; i < num_streams_; i++) {
        auto *stream = camera_config_->at(i).stream();
        streams_[i].stream = stream;
        if (allocator_->allocate(stream) < 0) {
            ERROR_PRINT("Can't allocate buffers");
        }

        auto &buffers = allocator_->buffers(stream);
        if (buffer_count_ != buffers.size()) {
            ERROR_PRINT("Buffer counts not match allocated buffer number");
            exit(1);
        }

        for (unsigned int j = 0; j < buffer_count_; j++) {
            int fd = 0;
            int buffer_length = 0;
            for (auto &plane : buffers[j]->planes()) {
                fd = plane.fd.get();
                buffer_length += plane.length;
            }
            void *memory = mmap(NULL, buffer_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            mapped_buffers_[fd] = std::make_pair(memory, buffer_length);
            DEBUG_PRINT("Allocated stream %d fd(%d) Buffer[%d] pointer: %p, length: %d", i, fd, j,
                        memory, buffer_length);
        }
    }

    // Each request carries one buffer of every stream, so both are filled from the same frame.
    for (unsigned int j = 0; j < buffer_count_; j++) {
        auto request = camera_->createRequest();
        if (!request) {
            ERROR_PRINT("Can't create camera request");
        }
        for (int i = 0; i < num_streams_; i++) {
            auto *stream = streams_[i].stream;
            if (request->addBuffer(stream, allocator_->buffers(stream)[j].get()) < 0) {
                ERROR_PRINT("Can't set buffer for request");
            }
        }
        requests_.push_back(std::move(request));
    }
}

void LibcameraCapturer::RequestComplete(libcamera::Request *request) {
    if (request->status() == libcamera::Request::RequestCancelled) {
        DEBUG_PRINT("Request has been cancelled");
        exit(1);
    }

    const bool traced = latency::Enabled();
    const int64_t callback_start_us = traced ? latency::NowUs() : 0;

    for (int i = 0; i < num_streams_; i++) {
        auto &stream = streams_[i];
        auto *buffer = request->findBuffer(stream.stream);
        if (!buffer) {
            continue;
        }

        int fd = buffer->planes()[0].fd.get();
        void *data = mapped_buffers_[fd].first;
        int length = mapped_buffers_[fd].second;
        timeval tv = {};
        tv.tv_sec = buffer->metadata().timestamp / 1000000000;
        tv.tv_usec = (buffer->metadata().timestamp % 1000000000) / 1000;

        auto v4l2_buffer = V4L2Buffer::FromLibcamera((uint8_t *)data, length, fd, tv, format_);
        stream.frame_buffer = V4L2FrameBuffer::Create(stream.width, stream.height, v4l2_buffer);

        if (traced && i == 0) {
            latency::RecordCapture(latency::SensorUs(tv), callback_start_us);
        }

        stream.subject.Next(stream.frame_buffer);
    }

    request->reuse(libcamera::Request::ReuseBuffers);

    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        if (is_controls_updated_) {
            request->controls().clear();
            request->controls().merge(controls_);
            is_controls_updated_ = false;
        }
    }

    if (traced) {
        latency::RecordSince(latency::Stage::kCaptureCallback, callback_start_us);
    }

    camera_->queueRequest(request);
}

webrtc::scoped_refptr<webrtc::I420BufferInterface> LibcameraCapturer::GetI420Frame(int stream_idx) {
    return StreamAt(stream_idx).frame_buffer->ToI420();
}

Subscription LibcameraCapturer::Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                          int stream_idx) {
    return StreamAt(stream_idx).subject.Subscribe(std::move(callback));
}

void LibcameraCapturer::StartCapture() {
    int ret = camera_->configure(camera_config_.get());
    if (ret < 0) {
        ERROR_PRINT("Failed to configure camera");
        exit(1);
    }

    AllocateBuffer();

    ret = camera_->start(&controls_);
    if (ret) {
        ERROR_PRINT("Failed to start capturing");
        exit(1);
    }

    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        controls_.clear();
    }

    camera_->requestCompleted.connect(this, &LibcameraCapturer::RequestComplete);

    for (auto &request : requests_) {
        ret = camera_->queueRequest(request.get());
        if (ret < 0) {
            ERROR_PRINT("Can't queue request");
            camera_->stop();
        }
    }
}
