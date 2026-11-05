#include "codecs/scaler_factory.h"

#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_scaler.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_scaler.h"
#endif

bool IsHwScalerAvailable() {
#if defined(USE_RPI_HW_ENCODER)
    return V4L2Scaler::IsAvailable();
#elif defined(USE_JETSON_HW_ENCODER)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IFrameProcessor> CreateHwScaler(ScalerConfig config) {
#if defined(USE_RPI_HW_ENCODER)
    return V4L2Scaler::Create(config);
#elif defined(USE_JETSON_HW_ENCODER)
    return JetsonScaler::Create(config);
#else
    return nullptr;
#endif
}
