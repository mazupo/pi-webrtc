#ifndef V4L2_SCALER_H_
#define V4L2_SCALER_H_

#include "codecs/v4l2/v4l2_codec.h"

class V4L2Scaler : public V4L2Codec {
  public:
    static std::unique_ptr<V4L2Scaler> Create(ScalerConfig config);
    static bool IsAvailable();

    V4L2Scaler(ScalerConfig config);

  protected:
    bool Initialize() override;

  private:
    ScalerConfig config_;
};

#endif // V4L2_SCALER_H_
