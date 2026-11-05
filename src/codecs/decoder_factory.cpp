#include "codecs/decoder_factory.h"

#include "codecs/ffmpeg/ffmpeg_decoder.h"
#include "codecs/jpeg/libyuv_mjpeg_decoder.h"
#include "common/logging.h"
#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_decoder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_decoder.h"
#endif

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
