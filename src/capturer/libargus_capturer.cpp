#include "libargus_capturer.h"

#include <cstdlib>
#include <string>

#include "common/latency_tracer.h"

#include <NvBufSurface.h>

namespace {

constexpr uint64_t kAcquireBufferTimeoutNs = 3'000'000'000;

timeval ToTimeval(uint64_t timestamp_ns) {
    timeval tv{};
    tv.tv_sec = timestamp_ns / 1000000000ULL;
    tv.tv_usec = (timestamp_ns % 1000000000ULL) / 1000ULL;
    return tv;
}

EGLDisplay GetEglDisplay() {
    return []() -> EGLDisplay {
        if (const char *x11 = getenv("DISPLAY"); x11 && *x11) {
            INFO_PRINT("Ignoring DISPLAY=%s; capturing on the Tegra device display.", x11);
            unsetenv("DISPLAY");
        }

        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) {
            ERROR_PRINT("Cannot get an EGL display for the buffer stream");
            return EGL_NO_DISPLAY;
        }
        if (eglInitialize(dpy, nullptr, nullptr) != EGL_TRUE) {
            ERROR_PRINT("Failed to initialize the EGL display");
            return EGL_NO_DISPLAY;
        }

        const char *vendor = eglQueryString(dpy, EGL_VENDOR);
        INFO_PRINT("EGL vendor: %s", vendor ? vendor : "unknown");
        return dpy;
    }();
}

} // namespace

std::shared_ptr<LibargusCapturer> LibargusCapturer::Create(Args args) {
    auto ptr = std::make_shared<LibargusCapturer>(args);
    ptr->Initialize();
    ptr->StartCapture();
    return ptr;
}

LibargusCapturer::LibargusCapturer(Args args)
    : camera_id_(args.camera_id),
      num_streams_(args.num_streams),
      fps_(args.fps),
      format_(V4L2_PIX_FMT_NV12),
      config_(args) {}

LibargusCapturer::~LibargusCapturer() {
    if (icapture_session_) {
        icapture_session_->stopRepeat();
        icapture_session_->waitForIdle();
    }

    stream_handlers_.clear();

    for (int i = 0; i < num_streams_; i++) {
        output_streams_[i].reset();
    }

    capture_session_.reset();
    camera_provider_.reset();
}

void LibargusCapturer::Initialize() {
    output_streams_.resize(num_streams_);

    stream_handlers_.emplace_back(
        StreamHandler::Create(0, Argus::Size2D<uint32_t>(config_.width, config_.height)));

    if (has_sub_stream()) {
        stream_handlers_.emplace_back(StreamHandler::Create(
            1, Argus::Size2D<uint32_t>(config_.sub_width, config_.sub_height)));
    }

    camera_provider_.reset(Argus::CameraProvider::create());
    if (!camera_provider_) {
        throw std::runtime_error("Failed to create CameraProvider");
    }

    InitStreams();
}

void LibargusCapturer::InitStreams() {
    auto icamera_provider = Argus::interface_cast<Argus::ICameraProvider>(camera_provider_);
    if (!icamera_provider) {
        throw std::runtime_error("Failed to create CameraProvider");
    }
    INFO_PRINT("Argus Version: %s", icamera_provider->getVersion().c_str());

    std::vector<Argus::CameraDevice *> camera_devices;
    if (icamera_provider->getCameraDevices(&camera_devices) != Argus::STATUS_OK ||
        camera_devices.empty()) {
        throw std::runtime_error("No cameras available");
    }
    if (camera_devices.size() <= static_cast<size_t>(camera_id_)) {
        throw std::runtime_error("Camera device " + std::to_string(camera_id_) +
                                 " requested but only " + std::to_string(camera_devices.size()) +
                                 " available.");
    }

    camera_device_ = camera_devices[camera_id_];
    capture_session_.reset(icamera_provider->createCaptureSession(camera_device_));
    icapture_session_ = Argus::interface_cast<Argus::ICaptureSession>(capture_session_);
    if (!icapture_session_) {
        throw std::runtime_error("Failed to get ICaptureSession");
    }

    Argus::UniqueObj<Argus::OutputStreamSettings> stream_settings(
        icapture_session_->createOutputStreamSettings(Argus::STREAM_TYPE_BUFFER));
    auto istream_settings =
        Argus::interface_cast<Argus::IBufferOutputStreamSettings>(stream_settings);
    if (!istream_settings) {
        throw std::runtime_error("Failed to get IBufferOutputStreamSettings");
    }

    istream_settings->setBufferType(Argus::BUFFER_TYPE_EGL_IMAGE);
    istream_settings->setMetadataEnable(true);

    for (int i = 0; i < num_streams_; i++) {
        output_streams_[i].reset(icapture_session_->createOutputStream(stream_settings.get()));
        if (!output_streams_[i]) {
            throw std::runtime_error("Failed to create Argus buffer output stream");
        }
        stream_handlers_[i]->SetOutputStream(output_streams_[i].get());
        if (!stream_handlers_[i]->PrepareBuffers()) {
            throw std::runtime_error("Failed to prepare capture buffers");
        }
    }

    request_ = Argus::UniqueObj<Argus::Request>(icapture_session_->createRequest());
    auto irequest = Argus::interface_cast<Argus::IRequest>(request_);
    for (int i = 0; i < num_streams_; i++) {
        irequest->enableOutputStream(output_streams_[i].get());
        auto irequest_stream_settings = Argus::interface_cast<Argus::IStreamSettings>(
            irequest->getStreamSettings(output_streams_[i].get()));
        if (!irequest_stream_settings) {
            throw std::runtime_error("Failed to get IStreamSettings");
        }
        irequest_stream_settings->setPostProcessingEnable(true);

        auto iac =
            Argus::interface_cast<Argus::IAutoControlSettings>(irequest->getAutoControlSettings());
        if (iac) {
            iac->setIspDigitalGainRange(Argus::Range<float>(1.0f, 1.2f));
        }
    }

    auto isource_settings =
        Argus::interface_cast<Argus::ISourceSettings>(irequest->getSourceSettings());
    if (!isource_settings) {
        throw std::runtime_error("Failed to get ISourceSettings");
    }
    auto mode = FindBestSensorMode(config_.width, config_.height, fps_);
    if (!mode) {
        INFO_PRINT("No exact sensor mode found for %dx%d@%dfps; Argus will use default mode.",
                   config_.width, config_.height, fps_);
    }
    isource_settings->setSensorMode(mode);
    isource_settings->setFrameDurationRange(
        Argus::Range<uint64_t>(static_cast<uint64_t>(1e9 / fps_)));
    float max_gain = (config_.gain > 0.0f) ? config_.gain : 8.0f;
    isource_settings->setGainRange(Argus::Range<float>(1.0f, max_gain));
}

bool StreamHandler::PrepareBuffers() {
    i_buffer_stream_ = Argus::interface_cast<Argus::IBufferOutputStream>(output_stream_);
    if (!i_buffer_stream_) {
        ERROR_PRINT("Failed to get IBufferOutputStream");
        return false;
    }

    egl_display_ = GetEglDisplay();
    if (egl_display_ == EGL_NO_DISPLAY) {
        return false;
    }

    Argus::UniqueObj<Argus::BufferSettings> buffer_settings(
        i_buffer_stream_->createBufferSettings());
    auto ibuffer_settings = Argus::interface_cast<Argus::IEGLImageBufferSettings>(buffer_settings);
    if (!ibuffer_settings) {
        ERROR_PRINT("Failed to create IEGLImageBufferSettings");
        return false;
    }

    frame_size_ = size_.width() * size_.height() * 3 / 2;

    for (int i = 0; i < kBufferCount; i++) {
        buffers_.push_back(std::make_unique<CaptureBuffer>());
        CaptureBuffer &buffer = *buffers_.back();

        // NV12 block-linear is what NVENC consumes, so nothing downstream has to convert.
        NvBufSurf::NvCommonAllocateParams params{};
        params.memtag = NvBufSurfaceTag_CAMERA;
        params.width = size_.width();
        params.height = size_.height();
        params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
        params.memType = NVBUF_MEM_SURFACE_ARRAY;

        if (NvBufSurf::NvAllocate(&params, 1, &buffer.dma_fd) < 0) {
            ERROR_PRINT("Failed to allocate NvBuffer for the capture pool");
            return false;
        }

        if (NvBufSurfaceFromFd(buffer.dma_fd, (void **)(&buffer.surface)) != 0 || !buffer.surface) {
            ERROR_PRINT("NvBufSurfaceFromFd failed for fd %d", buffer.dma_fd);
            return false;
        }

        if (NvBufSurfaceMapEglImage(buffer.surface, 0) != 0) {
            ERROR_PRINT("NvBufSurfaceMapEglImage failed for fd %d", buffer.dma_fd);
            return false;
        }

        EGLImageKHR egl_image = buffer.surface->surfaceList[0].mappedAddr.eglImage;
        if (egl_image == EGL_NO_IMAGE_KHR) {
            ERROR_PRINT("Failed to map an EGLImage for fd %d", buffer.dma_fd);
            return false;
        }

        ibuffer_settings->setEGLImage(egl_image);
        ibuffer_settings->setEGLDisplay(egl_display_);
        buffer.argus_buffer.reset(i_buffer_stream_->createBuffer(buffer_settings.get()));

        auto ibuffer = Argus::interface_cast<Argus::IBuffer>(buffer.argus_buffer);
        if (!ibuffer || !Argus::interface_cast<Argus::IEGLImageBuffer>(buffer.argus_buffer)) {
            ERROR_PRINT("Failed to create an Argus Buffer");
            return false;
        }

        // The acquired Buffer is matched back to its dmabuf through this pointer.
        ibuffer->setClientData(&buffer);

        // Hand the buffer to Argus so it can be filled by the first capture.
        if (i_buffer_stream_->releaseBuffer(buffer.argus_buffer.get()) != Argus::STATUS_OK) {
            ERROR_PRINT("Failed to release a Buffer for capture use");
            return false;
        }
    }

    last_frame_buffer_ = WrapBuffer(buffers_[0]->dma_fd, {0, 0});
    return true;
}

V4L2FrameBufferRef StreamHandler::WrapBuffer(int dma_fd, timeval timestamp) const {
    auto v4l2_buffer =
        V4L2Buffer::FromCapturedPlane(nullptr, frame_size_, dma_fd, 0, V4L2_PIX_FMT_NV12);
    v4l2_buffer.timestamp = timestamp;
    return V4L2FrameBuffer::Create(size_.width(), size_.height(), v4l2_buffer);
}

void StreamHandler::CaptureImage() {
    Argus::Status status = Argus::STATUS_OK;
    Argus::Buffer *buffer = i_buffer_stream_->acquireBuffer(kAcquireBufferTimeoutNs, &status);
    if (status != Argus::STATUS_OK || !buffer) {
        return;
    }

    auto ibuffer = Argus::interface_cast<Argus::IBuffer>(buffer);
    if (!ibuffer) {
        i_buffer_stream_->releaseBuffer(buffer);
        return;
    }

    auto *capture_buffer =
        static_cast<CaptureBuffer *>(const_cast<void *>(ibuffer->getClientData()));
    if (!capture_buffer) {
        i_buffer_stream_->releaseBuffer(buffer);
        return;
    }

    // Argus wrote this capture directly into our NvBufSurface, so there is no copy here at all.
    // The fd handed downstream is the one the sensor pipeline just filled.
    timeval timestamp{};
    if (auto *metadata = Argus::interface_cast<const Argus::ICaptureMetadata>(
            const_cast<Argus::CaptureMetadata *>(ibuffer->getMetadata()))) {
        timestamp = ToTimeval(metadata->getSensorTimestamp());
    }
    // A fresh handle per capture. It costs one small allocation and keeps consumers that are still
    // holding the previous frame from seeing its timestamp rewritten when Argus hands the same pool
    // slot back a few captures later.
    auto frame_buffer = WrapBuffer(capture_buffer->dma_fd, timestamp);

    if (latency::Enabled()) {
        latency::RecordCapture(latency::SensorUs(timestamp), latency::NowUs());
    }

    last_frame_buffer_ = frame_buffer;
    Next(frame_buffer);

    i_buffer_stream_->releaseBuffer(buffer);
}

void StreamHandler::StartCapture() {
    if (!output_stream_ || !i_buffer_stream_) {
        ERROR_PRINT("Buffer output stream is null");
        return;
    }

    INFO_PRINT("stream %d initialized: %dx%d (zero-copy, %d buffers)", stream_idx_, width(),
               height(), kBufferCount);

    worker_ =
        std::make_unique<Worker>("argus buffer stream: " + std::to_string(stream_idx_), [this]() {
            while (running_) {
                CaptureImage();
            }
        });
    worker_->Run();
}

void StreamHandler::ReleaseBuffers() {
    for (auto &buffer : buffers_) {
        buffer->argus_buffer.reset();

        if (buffer->surface) {
            NvBufSurfaceUnMapEglImage(buffer->surface, 0);
            buffer->surface = nullptr;
        }
        if (buffer->dma_fd >= 0) {
            NvBufSurf::NvDestroy(buffer->dma_fd);
            buffer->dma_fd = -1;
        }
    }
    buffers_.clear();
    egl_display_ = EGL_NO_DISPLAY;
}

StreamHandler::~StreamHandler() {
    running_ = false;
    if (i_buffer_stream_) {
        // Unblocks a worker parked in acquireBuffer().
        i_buffer_stream_->endOfStream();
    }
    worker_.reset();
    last_frame_buffer_ = nullptr;
    ReleaseBuffers();
}

int LibargusCapturer::fps() const { return fps_; }

int LibargusCapturer::width(int stream_idx) const {
    if (stream_idx <= 0 || stream_idx >= num_streams_) {
        return stream_handlers_[0]->width();
    }
    return stream_handlers_[stream_idx]->width();
}

int LibargusCapturer::height(int stream_idx) const {
    if (stream_idx <= 0 || stream_idx >= num_streams_) {
        return stream_handlers_[0]->height();
    }
    return stream_handlers_[stream_idx]->height();
}

bool LibargusCapturer::is_dma_capture() const { return true; }

uint32_t LibargusCapturer::format() const { return format_; }

Args LibargusCapturer::config() const { return config_; }

webrtc::scoped_refptr<webrtc::I420BufferInterface> LibargusCapturer::GetI420Frame(int stream_idx) {
    if (stream_idx <= 0 || stream_idx >= num_streams_) {
        return stream_handlers_[0]->GetFrameBuffer()->ToI420();
    }
    return stream_handlers_[stream_idx]->GetFrameBuffer()->ToI420();
}

void LibargusCapturer::StartCapture() {
    for (auto &handler : stream_handlers_) {
        handler->StartCapture();
    }

    if (icapture_session_->repeat(request_.get()) != Argus::STATUS_OK) {
        throw std::runtime_error("Failed to start repeat");
    }
}

Subscription LibargusCapturer::Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                         int stream_idx) {
    if (stream_idx <= 0 || stream_idx >= num_streams_) {
        return stream_handlers_[0]->Subscribe(std::move(callback));
    }
    return stream_handlers_[stream_idx]->Subscribe(std::move(callback));
}

Argus::SensorMode *LibargusCapturer::FindBestSensorMode(int req_width, int req_height,
                                                        int req_fps) {
    auto icamera_properties = Argus::interface_cast<Argus::ICameraProperties>(camera_device_);
    std::vector<Argus::SensorMode *> sensor_modes;
    icamera_properties->getAllSensorModes(&sensor_modes);

    for (auto mode : sensor_modes) {
        auto imode = Argus::interface_cast<Argus::ISensorMode>(mode);
        if (!imode) {
            continue;
        }
        auto resolution = imode->getResolution();
        auto duration = imode->getFrameDurationRange();
        Argus::Range<uint64_t> desired(static_cast<uint64_t>(1e9 / req_fps + 0.5));
        if (resolution.width() == req_width && resolution.height() == req_height &&
            desired.max() >= duration.min() && desired.min() <= duration.max()) {
            return mode;
        }
    }
    return nullptr;
}
