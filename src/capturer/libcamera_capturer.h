#ifndef LIBCAMERA_CAPTURER_H_
#define LIBCAMERA_CAPTURER_H_

#include <array>
#include <vector>

#include <libcamera/libcamera.h>

#include "capturer/capture_config.h"
#include "capturer/video_capturer.h"
#include "common/subject.h"
#include "common/v4l2_frame_buffer.h"

class LibcameraCapturer : public VideoCapturer {
  public:
    static std::shared_ptr<LibcameraCapturer> Create(VideoCaptureConfig config);

    LibcameraCapturer(VideoCaptureConfig config);
    ~LibcameraCapturer() override;

    int fps() const override;
    int width(int stream_idx = 0) const override;
    int height(int stream_idx = 0) const override;
    bool has_sub_stream() const override;
    uint32_t format() const override;

    bool SetControls(int key, int value) override;

    webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) override;
    Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                           int stream_idx = 0) override;

  private:
    // The main stream, and the optional sub-stream the ISP scales from the same frame.
    static constexpr int kMaxStreams = 2;

    struct StreamState {
        libcamera::Stream *stream = nullptr;
        int width = 0;
        int height = 0;
        V4L2FrameBufferRef frame_buffer;
        Subject<V4L2FrameBufferRef> subject;
    };

    int camera_id_;
    int num_streams_;
    int fps_;
    int rotation_;
    int buffer_count_;
    uint32_t format_;
    std::mutex control_mutex_;
    std::atomic<bool> is_controls_updated_;

    std::unique_ptr<libcamera::CameraManager> cm_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::CameraConfiguration> camera_config_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;
    std::vector<std::unique_ptr<libcamera::Request>> requests_;
    std::array<StreamState, kMaxStreams> streams_;
    libcamera::ControlList controls_;
    std::map<int, std::pair<void *, unsigned int>> mapped_buffers_;

    /** The stream an index refers to; anything out of range is the main stream. */
    const StreamState &StreamAt(int stream_idx) const;
    StreamState &StreamAt(int stream_idx);

    void InitCamera();
    void InitControls(CameraControls requested);
    void AllocateBuffer();
    void StartCapture() override;
    void RequestComplete(libcamera::Request *request);
};

#endif
