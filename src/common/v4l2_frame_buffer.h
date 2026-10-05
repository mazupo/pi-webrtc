#ifndef COMMON_V4L2_FRAME_BUFFER_H_
#define COMMON_V4L2_FRAME_BUFFER_H_

#include "common/v4l2_utils.h"

#include <cstdint>
#include <linux/videodev2.h>
#include <vector>

#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <common_video/include/video_frame_buffer.h>
#include <rtc_base/memory/aligned_malloc.h>

class V4L2FrameBuffer : public webrtc::VideoFrameBuffer {
  public:
    static webrtc::scoped_refptr<V4L2FrameBuffer> Create(int width, int height, int size,
                                                         uint32_t format);
    static webrtc::scoped_refptr<V4L2FrameBuffer> Create(int width, int height, V4L2Buffer buffer);

    Type type() const override;
    int width() const override;
    int height() const override;
    webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;

    uint32_t format() const;
    uint32_t size() const;
    // Bytes per luma row and rows per plane; larger than width/height when the producer pads
    // them, e.g. the Pi decoder stores 1080 rows as 1088.
    int stride() const;
    int plane_height() const;
    void SetLayout(int stride, int plane_height);
    uint32_t flags() const;
    timeval timestamp() const;

    const void *Data() const;
    bool IsDmaOnly() const;
    uint8_t *MutableData();
    V4L2Buffer GetRawBuffer();
    int GetDmaFd() const;
    void SetDmaFd(int fd);
    void SetTimestamp(timeval timestamp);
    webrtc::scoped_refptr<V4L2FrameBuffer> Clone() const;

  protected:
    V4L2FrameBuffer(int width, int height, int size, uint32_t format);
    V4L2FrameBuffer(int width, int height, V4L2Buffer buffer);
    ~V4L2FrameBuffer() override;

  private:
    const int width_;
    const int height_;
    const uint32_t format_;
    uint32_t size_;
    int stride_;
    int plane_height_;
    uint32_t flags_;
    timeval timestamp_;
    V4L2Buffer buffer_;
    std::unique_ptr<uint8_t, webrtc::AlignedFreeDeleter> data_;

    V4L2FrameBuffer(int width, int height, uint32_t format, int size, uint32_t flags,
                    timeval timestamp);
};

using V4L2FrameBufferRef = webrtc::scoped_refptr<V4L2FrameBuffer>;

#endif // COMMON_V4L2_FRAME_BUFFER_H_
