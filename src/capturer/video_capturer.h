#ifndef VIDEO_CAPTURER_H_
#define VIDEO_CAPTURER_H_

#include "args.h"
#include "common/subject.h"
#include "common/v4l2_frame_buffer.h"

// multiple video stream capturer interface
class VideoCapturer {
  public:
    VideoCapturer() = default;
    virtual ~VideoCapturer() = default;

    virtual int fps() const = 0;
    virtual int width(int stream_idx = 0) const = 0;
    virtual int height(int stream_idx = 0) const = 0;
    virtual bool has_sub_stream() const { return false; }
    virtual bool is_dma_capture() const = 0;
    virtual uint32_t format() const = 0;
    virtual Args config() const = 0;
    virtual void StartCapture() = 0;
    virtual webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) = 0;
    virtual bool SetControls(int key, int value) { return false; };
    virtual Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                   int stream_idx = 0) = 0;
};

#endif
