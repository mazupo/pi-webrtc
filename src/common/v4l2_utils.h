#ifndef COMMON_V4L2_UTILS_H_
#define COMMON_V4L2_UTILS_H_

#include <linux/videodev2.h>
#include <stdint.h>
#include <vector>

#include "common/v4l2_buffer.h"

struct V4L2BufferGroup {
    int fd = -1;
    uint32_t num_planes = 0;
    uint32_t num_buffers = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytesperline = 0;
    uint32_t sizeimage = 0;
    bool has_dmafd = false;
    std::vector<V4L2Buffer> buffers;
    enum v4l2_buf_type type;
    enum v4l2_memory memory;
};

namespace v4l2_util {

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
               uint32_t &pixel_format, uint32_t sizeimage = 0, bool allow_padding = false);
bool SetCrop(int fd, v4l2_buf_type type, uint32_t width, uint32_t height);
bool SetCtrl(int fd, uint32_t id, int32_t value);
bool SetExtCtrl(int fd, uint32_t id, int32_t value);
bool StreamOn(int fd, v4l2_buf_type type);
bool StreamOff(int fd, v4l2_buf_type type);
bool AllocateBuffer(int fd, V4L2BufferGroup *gbuffer, int num_buffers);
bool DeallocateBuffer(int fd, V4L2BufferGroup *gbuffer);

} // namespace v4l2_util

#endif // COMMON_V4L2_UTILS_H_
