#ifndef DECODER_FACTORY_H_
#define DECODER_FACTORY_H_

#include <memory>

#include "codecs/frame_processor.h"

// Tries hardware when is_dma_dst is set, then software; MJPEG without is_dma_dst gets none.
std::unique_ptr<FrameProcessor> CreateVideoDecoder(DecoderConfig config);

#endif // DECODER_FACTORY_H_
