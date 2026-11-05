#ifndef DECODER_FACTORY_H_
#define DECODER_FACTORY_H_

#include <memory>

#include "codecs/frame_processor.h"

struct VideoDecoder {
    std::unique_ptr<IFrameProcessor> processor;
    // True when frames come out of a hardware decoder as DMA buffers.
    bool is_hardware = false;
};

// Tries hardware when is_dma_dst is set, then software; MJPEG without is_dma_dst gets none.
VideoDecoder CreateVideoDecoder(DecoderConfig config);

#endif // DECODER_FACTORY_H_
