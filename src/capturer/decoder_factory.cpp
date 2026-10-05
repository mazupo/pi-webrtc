#include "capturer/decoder_factory.h"

#include <third_party/libyuv/include/libyuv.h>

#include "codecs/ffmpeg/ffmpeg_decoder.h"
#include "common/logging.h"
#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_decoder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_decoder.h"
#endif

namespace {

// Decodes MJPEG to I420 for the hardware scaler and encoder, which cannot take JPEG.
class LibyuvMjpegDecoder : public IFrameProcessor {
  public:
    explicit LibyuvMjpegDecoder(DecoderConfig config)
        : config_(config) {}

    void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override {
        int width = config_.width;
        int height = config_.height;
        int y_size = width * height;
        auto decoded = V4L2FrameBuffer::Create(width, height, y_size * 3 / 2, V4L2_PIX_FMT_YUV420);
        uint8_t *y = decoded->MutableData();
        uint8_t *u = y + y_size;
        uint8_t *v = u + y_size / 4;
        if (libyuv::MJPGToI420(static_cast<const uint8_t *>(frame_buffer->Data()),
                               frame_buffer->size(), y, width, u, width / 2, v, width / 2, width,
                               height, width, height) != 0) {
            return;
        }
        decoded->SetTimestamp(frame_buffer->timestamp());
        on_capture(decoded);
    }

  protected:
    bool Initialize() override { return true; }

  private:
    DecoderConfig config_;
};

} // namespace

VideoDecoder CreateVideoDecoder(DecoderConfig config) {
    VideoDecoder decoder;
    if (config.is_dma_dst) {
#if defined(USE_RPI_HW_ENCODER)
        decoder.processor = V4L2Decoder::Create(config);
#elif defined(USE_JETSON_HW_ENCODER)
        decoder.processor = JetsonDecoder::Create(config);
#endif
        if (decoder.processor) {
            decoder.is_hardware = true;
            return decoder;
        }
    }
    if (config.src_pix_fmt == V4L2_PIX_FMT_H264 || config.src_pix_fmt == V4L2_PIX_FMT_HEVC) {
        decoder.processor = FfmpegDecoder::Create(config, config.is_dma_dst);
    } else if (config.src_pix_fmt == V4L2_PIX_FMT_MJPEG && config.is_dma_dst) {
        INFO_PRINT("Decoding MJPEG with libyuv.");
        decoder.processor = std::make_unique<LibyuvMjpegDecoder>(config);
    }
    return decoder;
}
