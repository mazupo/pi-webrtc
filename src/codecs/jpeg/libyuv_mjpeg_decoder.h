#ifndef LIBYUV_MJPEG_DECODER_H_
#define LIBYUV_MJPEG_DECODER_H_

#include "codecs/frame_processor.h"

// Decodes MJPEG to I420 for the hardware scaler and encoder, which cannot take JPEG.
class LibyuvMjpegDecoder : public FrameProcessor {
  public:
    explicit LibyuvMjpegDecoder(DecoderConfig config);

    void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override;

  protected:
    bool Initialize() override;

  private:
    DecoderConfig config_;
};

#endif // LIBYUV_MJPEG_DECODER_H_
