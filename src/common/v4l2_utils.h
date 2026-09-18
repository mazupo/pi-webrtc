#ifndef COMMON_V4L2_UTILS_H_
#define COMMON_V4L2_UTILS_H_

#include <linux/videodev2.h>
#include <stdint.h>
#include <string>
#include <vector>

/* Save single-plane data with stride equal to width */
struct V4L2Buffer {
    void *start = nullptr;
    uint32_t pix_fmt = 0;
    uint32_t length = 0;
    uint32_t flags = 0;
    int dmafd = -1;
    struct timeval timestamp = {0, 0};
    struct v4l2_buffer inner = {};
    struct v4l2_plane plane[VIDEO_MAX_PLANES];

    V4L2Buffer() = default;

    V4L2Buffer(void *data, uint32_t fmt, uint32_t len, int fd, uint32_t f, timeval ts)
        : start(data),
          pix_fmt(fmt),
          length(len),
          flags(f),
          dmafd(fd),
          timestamp(ts) {}

    static V4L2Buffer FromV4L2(void *start, const v4l2_buffer &v4l2, uint32_t fmt) {
        V4L2Buffer buf(start, fmt, v4l2.bytesused, -1, v4l2.flags, v4l2.timestamp);
        buf.inner = v4l2;
        return buf;
    }

    static V4L2Buffer FromLibcamera(void *start, int length, int dmafd, timeval timestamp,
                                    uint32_t fmt) {
        return V4L2Buffer(start, fmt, length, dmafd, 0, timestamp);
    }

    static V4L2Buffer FromCapturedPlane(void *start, uint32_t bytesused, int dmafd, uint32_t flags,
                                        uint32_t fmt) {
        return V4L2Buffer(start, fmt, bytesused, dmafd, flags, {0, 0});
    }
};

struct V4L2BufferGroup {
    int fd = -1;
    uint32_t num_planes = 0;
    uint32_t num_buffers = 0;
    bool has_dmafd = false;
    std::vector<V4L2Buffer> buffers;
    enum v4l2_buf_type type;
    enum v4l2_memory memory;
};

namespace v4l2_util {

std::string FourccToString(uint32_t fourcc);
int OpenDevice(const char *file);
void CloseDevice(int fd);
bool QueryCapabilities(int fd, v4l2_capability *cap);
bool IsM2MDeviceReady(const char *file);
bool InitBuffer(int fd, V4L2BufferGroup *gbuffer, v4l2_buf_type type, v4l2_memory memory,
                bool has_dmafd = false);
bool DequeueBuffer(int fd, v4l2_buffer *buffer);
bool QueueBuffer(int fd, v4l2_buffer *buffer);
bool QueueBuffers(int fd, V4L2BufferGroup *gbuffer);
bool SubscribeEvent(int fd, uint32_t type);
bool SetFps(int fd, v4l2_buf_type type, uint32_t fps);
bool SetFormat(int fd, V4L2BufferGroup *gbuffer, uint32_t width, uint32_t height,
               uint32_t &pixel_format);
bool SetCtrl(int fd, uint32_t id, int32_t value);
bool SetExtCtrl(int fd, uint32_t id, int32_t value);
bool StreamOn(int fd, v4l2_buf_type type);
bool StreamOff(int fd, v4l2_buf_type type);
bool AllocateBuffer(int fd, V4L2BufferGroup *gbuffer, int num_buffers);
bool DeallocateBuffer(int fd, V4L2BufferGroup *gbuffer);

} // namespace v4l2_util

#endif // COMMON_V4L2_UTILS_H_
