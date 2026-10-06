#include "codecs/v4l2/v4l2_decoder.h"
#include "common/logging.h"

constexpr const char *DECODER_FILE = "/dev/video10";
constexpr int OUTPUT_BUFFER_NUM = 2;
constexpr int CAPTURE_BUFFER_NUM = 4;
constexpr int MAX_WIDTH = 1920;
constexpr int MAX_HEIGHT = 1088;
// The driver's default (768 KiB at 1080p) is too small for some RTSP keyframes.
constexpr uint32_t INPUT_BUFFER_SIZE = 2 * 1024 * 1024;
// A dropped compressed frame corrupts the picture until the next keyframe, so wait instead.
constexpr int INPUT_WAIT_MS = 200;

std::unique_ptr<V4L2Decoder> V4L2Decoder::Create(DecoderConfig config) {
    if (!IsAvailable()) {
        WARN_PRINT("No V4L2 hardware decoder at %s.", DECODER_FILE);
        return nullptr;
    }
    if (config.src_pix_fmt != V4L2_PIX_FMT_H264 && config.src_pix_fmt != V4L2_PIX_FMT_MJPEG) {
        WARN_PRINT("The V4L2 hardware decoder does not support %s.",
                   v4l2_util::FourccToString(config.src_pix_fmt).c_str());
        return nullptr;
    }
    if (config.width > MAX_WIDTH || config.height > MAX_HEIGHT) {
        WARN_PRINT("The V4L2 hardware decoder supports up to %dx%d, not %dx%d.", MAX_WIDTH,
                   MAX_HEIGHT, config.width, config.height);
        return nullptr;
    }
    auto decoder = std::make_unique<V4L2Decoder>(config);
    if (!decoder->Initialize()) {
        return nullptr;
    }
    decoder->Start();
    return decoder;
}

bool V4L2Decoder::IsAvailable() { return v4l2_util::IsM2MDeviceReady(DECODER_FILE); }

V4L2Decoder::V4L2Decoder(DecoderConfig config)
    : V4L2Codec(),
      config_(config) {
    input_wait_ms_ = INPUT_WAIT_MS;
}

bool V4L2Decoder::Initialize() {
    if (!Open(DECODER_FILE)) {
        ERROR_PRINT("Unable to turn on decoder: %s", DECODER_FILE);
        return false;
    }

    if (!SetupOutputBuffer(config_.width, config_.height, config_.src_pix_fmt, V4L2_MEMORY_MMAP,
                           OUTPUT_BUFFER_NUM, INPUT_BUFFER_SIZE)) {
        ERROR_PRINT("Could not setup output buffer");
        return false;
    }
    // NV12 rows align the same on the decoder, encoder and ISP, so they share buffers at any
    // width. The decoder pads the height to a multiple of 16 and leaves the padding rows blank.
    if (!SetupCaptureBuffer(config_.width, config_.height, V4L2_PIX_FMT_NV12, V4L2_MEMORY_MMAP,
                            CAPTURE_BUFFER_NUM, config_.is_dma_dst, true)) {
        ERROR_PRINT("Could not setup capture buffer");
        return false;
    }
    if (capture_plane_height() != config_.height) {
        INFO_PRINT("Hardware decoder pads %dx%d to %d rows; the encoder and ISP skip them.",
                   config_.width, config_.height, capture_plane_height());
    }

    if (!SubscribeEvent(V4L2_EVENT_SOURCE_CHANGE)) {
        ERROR_PRINT("Could not subscribe source change event");
    }
    if (!SubscribeEvent(V4L2_EVENT_EOS)) {
        ERROR_PRINT("Could not subscribe EOS event");
    }

    return true;
}
