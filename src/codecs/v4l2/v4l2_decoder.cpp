#include "codecs/v4l2/v4l2_decoder.h"
#include "common/logging.h"

constexpr const char *DECODER_FILE = "/dev/video10";
constexpr int BUFFER_NUM = 2;

std::unique_ptr<V4L2Decoder> V4L2Decoder::Create(DecoderConfig config) {
    auto decoder = std::make_unique<V4L2Decoder>(config);
    if (!decoder->Initialize()) {
        return nullptr;
    }
    decoder->Start();
    return decoder;
}

V4L2Decoder::V4L2Decoder(DecoderConfig config)
    : V4L2Codec(),
      config_(config) {}

bool V4L2Decoder::Initialize() {
    if (!Open(DECODER_FILE)) {
        ERROR_PRINT("Unable to turn on decoder: %s", DECODER_FILE);
        return false;
    }

    if (!SetupOutputBuffer(config_.width, config_.height, config_.src_pix_fmt, V4L2_MEMORY_MMAP,
                           BUFFER_NUM)) {
        ERROR_PRINT("Could not setup output buffer");
        return false;
    }
    if (!SetupCaptureBuffer(config_.width, config_.height, V4L2_PIX_FMT_YUV420, V4L2_MEMORY_MMAP,
                            BUFFER_NUM, config_.is_dma_dst)) {
        ERROR_PRINT("Could not setup capture buffer");
        return false;
    }

    if (!SubscribeEvent(V4L2_EVENT_SOURCE_CHANGE)) {
        ERROR_PRINT("Could not subscribe source change event");
    }
    if (!SubscribeEvent(V4L2_EVENT_EOS)) {
        ERROR_PRINT("Could not subscribe EOS event");
    }

    return true;
}
