#ifndef V4L2_CAPTURER_H_
#define V4L2_CAPTURER_H_

#include <modules/video_capture/video_capture.h>

#include "args.h"
#include "capturer/video_capturer.h"
#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_decoder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_decoder.h"
#endif
#include "common/interface/subject.h"
#include "common/v4l2_frame_buffer.h"
#include "common/v4l2_utils.h"
#include "common/worker.h"

class V4L2Capturer : public VideoCapturer {
  public:
    static std::shared_ptr<V4L2Capturer> Create(Args args);

    V4L2Capturer(Args args);
    ~V4L2Capturer() override;

    int fps() const override;
    int width(int stream_idx = 0) const override;
    int height(int stream_idx = 0) const override;
    bool is_dma_capture() const override;
    uint32_t format() const override;
    Args config() const override;

    bool SetControls(int key, int value) override;
    void StartCapture() override;

    webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) override;
    Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                           int stream_idx = 0) override;

  private:
    int camera_id_;
    int fd_;
    int fps_;
    int width_;
    int height_;
    int rotation_;
    int buffer_count_;
    bool hw_accel_;
    bool has_first_keyframe_;
    uint32_t format_;
    Args config_;
    V4L2BufferGroup capture_;
    std::unique_ptr<Worker> worker_;
    std::unique_ptr<IFrameProcessor> decoder_;

    V4L2FrameBufferRef frame_buffer_;
    Subject<V4L2FrameBufferRef> stream_subject_;

    void Initialize();
    bool IsCompressedFormat() const;
    void CaptureImage();
    bool CheckMatchingDevice(std::string unique_name);
    int GetCameraIndex(webrtc::VideoCaptureModule::DeviceInfo *device_info);
};

#endif
