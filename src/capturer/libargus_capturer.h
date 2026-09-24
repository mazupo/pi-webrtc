#ifndef LIBARGUS_CAPTURER_H_
#define LIBARGUS_CAPTURER_H_

#include <vector>

#include "args.h"
#include "capturer/video_capturer.h"
#include "common/logging.h"
#include "common/worker.h"

// Jetson Multimedia API
#include <Argus/Argus.h>
#include <Argus/BufferStream.h>
#include <Argus/EGLImage.h>
#include <Argus/Ext/DolWdrSensorMode.h>
#include <Argus/Ext/PwlWdrSensorMode.h>
#include <Argus/Stream.h>
#include <EGL/egl.h>
#include <nvbufsurface.h>

class StreamHandler : public Subject<V4L2FrameBufferRef> {
  public:
    static constexpr int kBufferCount = 6;

    static std::unique_ptr<StreamHandler> Create(int stream_idx, Argus::Size2D<uint32_t> size) {
        return std::make_unique<StreamHandler>(stream_idx, size);
    }

    StreamHandler(int stream_idx, Argus::Size2D<uint32_t> size)
        : stream_idx_(stream_idx),
          size_(size),
          running_(true) {}

    ~StreamHandler();

    uint32_t width() const { return size_.width(); }
    uint32_t height() const { return size_.height(); }
    Argus::Size2D<uint32_t> GetSize() const { return size_; }

    V4L2FrameBufferRef GetFrameBuffer() { return last_frame_buffer_; }
    void SetOutputStream(Argus::OutputStream *stream) { output_stream_ = stream; }
    bool PrepareBuffers();
    void StartCapture();

  private:
    struct CaptureBuffer {
        int dma_fd = -1;
        NvBufSurface *surface = nullptr;
        Argus::UniqueObj<Argus::Buffer> argus_buffer;
    };

    int stream_idx_;
    Argus::Size2D<uint32_t> size_;
    uint32_t frame_size_ = 0;
    std::atomic<bool> running_;

    Argus::OutputStream *output_stream_ = nullptr;
    Argus::IBufferOutputStream *i_buffer_stream_ = nullptr;

    EGLDisplay egl_display_ = EGL_NO_DISPLAY;

    std::vector<std::unique_ptr<CaptureBuffer>> buffers_;
    V4L2FrameBufferRef last_frame_buffer_;

    std::unique_ptr<Worker> worker_;

    V4L2FrameBufferRef WrapBuffer(int dma_fd, timeval timestamp) const;
    void CaptureImage();
    void ReleaseBuffers();
};

class LibargusCapturer : public VideoCapturer {
  public:
    static std::shared_ptr<LibargusCapturer> Create(Args args);

    LibargusCapturer(Args args);
    ~LibargusCapturer();

    int fps() const override;
    int width(int stream_idx = 0) const override;
    int height(int stream_idx = 0) const override;
    bool has_sub_stream() const override { return num_streams_ > 1; }
    bool is_dma_capture() const override;
    uint32_t format() const override;
    Args config() const override;

    webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) override;
    void StartCapture() override;

    Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                           int stream_idx = 0) override;

  private:
    int camera_id_;
    int num_streams_;
    int fps_;
    uint32_t format_;
    Args config_;

    Argus::CameraDevice *camera_device_ = nullptr;
    Argus::UniqueObj<Argus::CameraProvider> camera_provider_;
    Argus::UniqueObj<Argus::CaptureSession> capture_session_;
    Argus::ICaptureSession *icapture_session_ = nullptr;
    Argus::UniqueObj<Argus::Request> request_;
    std::vector<Argus::UniqueObj<Argus::OutputStream>> output_streams_;

    std::vector<std::unique_ptr<StreamHandler>> stream_handlers_;

    void Initialize();
    void InitStreams();

    Argus::SensorMode *FindBestSensorMode(int req_width, int req_height, int req_fps);
};

#endif // LIBARGUS_CAPTURER_H_
