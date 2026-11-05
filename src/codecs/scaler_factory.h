#ifndef SCALER_FACTORY_H_
#define SCALER_FACTORY_H_

#include <memory>

#include "codecs/frame_processor.h"

// False when the device has no hardware scaler.
bool IsHwScalerAvailable();

// Returns nullptr when the hardware scaler cannot be opened.
std::unique_ptr<FrameProcessor> CreateHwScaler(ScalerConfig config);

#endif // SCALER_FACTORY_H_
