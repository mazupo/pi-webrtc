#include "codecs/jpeg/libyuv_mjpeg_decoder.h"

#include <third_party/libyuv/include/libyuv.h>

LibyuvMjpegDecoder::LibyuvMjpegDecoder(DecoderConfig config)
    : config_(config) {}

void LibyuvMjpegDecoder::EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                                       std::function<void(V4L2FrameBufferRef)> on_capture) {
    int width = config_.width;
    int height = config_.height;
    int y_size = width * height;
    auto decoded = V4L2FrameBuffer::Create(width, height, y_size * 3 / 2, V4L2_PIX_FMT_YUV420);
    uint8_t *y = decoded->MutableData();
    uint8_t *u = y + y_size;
    uint8_t *v = u + y_size / 4;
    if (libyuv::MJPGToI420(static_cast<const uint8_t *>(frame_buffer->Data()), frame_buffer->size(),
                           y, width, u, width / 2, v, width / 2, width, height, width,
                           height) != 0) {
        return;
    }
    decoded->SetTimestamp(frame_buffer->timestamp());
    on_capture(decoded);
}

bool LibyuvMjpegDecoder::Initialize() { return true; }
