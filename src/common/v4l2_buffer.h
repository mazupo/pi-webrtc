#ifndef COMMON_V4L2_BUFFER_H_
#define COMMON_V4L2_BUFFER_H_

#include <linux/videodev2.h>
#include <stdint.h>
#include <string>
#include <sys/time.h>

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

namespace v4l2_util {

inline std::string FourccToString(uint32_t fourcc) {
    std::string buf(4, '\0');
    for (int i = 0; i < 4; i++) {
        buf[i] = fourcc & 0xff;
        fourcc >>= 8;
    }
    return buf;
}

} // namespace v4l2_util

#endif // COMMON_V4L2_BUFFER_H_
