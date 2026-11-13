#ifndef FRAME_SOURCE_H_
#define FRAME_SOURCE_H_

#include <cstdint>

#include "common/subject.h"
#include "common/v4l2_frame_buffer.h"

// A stream of video frames to subscribe to, from a capturer or a stage layered on one.
class FrameSource {
  public:
    virtual ~FrameSource() = default;

    virtual int fps() const = 0;
    virtual int width(int stream_idx = 0) const = 0;
    virtual int height(int stream_idx = 0) const = 0;
    virtual bool has_sub_stream() const { return false; }
    virtual uint32_t format() const = 0;
    virtual webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) = 0;
    virtual Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                                   int stream_idx = 0) = 0;
};

#endif // FRAME_SOURCE_H_
