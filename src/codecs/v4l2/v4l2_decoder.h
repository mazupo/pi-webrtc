#ifndef V4L2_DECODER_H_
#define V4L2_DECODER_H_

#include "codecs/v4l2/v4l2_codec.h"

class V4L2Decoder : public V4L2Codec {
  public:
    static std::unique_ptr<V4L2Decoder> Create(DecoderConfig config);
    static bool IsAvailable();
    V4L2Decoder(DecoderConfig config);

  protected:
    bool Initialize() override;

  private:
    DecoderConfig config_;
};

#endif // V4L2_DECODER_H_
