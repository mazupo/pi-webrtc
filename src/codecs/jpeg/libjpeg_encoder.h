#ifndef LIBJPEG_ENCODER_H_
#define LIBJPEG_ENCODER_H_

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

namespace libjpeg_encoder {

struct FreeDeleter {
    void operator()(uint8_t *ptr) const {
        if (ptr) {
            free(ptr);
        }
    }
};

/* An owned, malloc-allocated JPEG blob, as produced by libjpeg's memory destination. */
struct JpegBuffer {
    std::unique_ptr<uint8_t, FreeDeleter> start;
    unsigned long length;
};

JpegBuffer ConvertYuvToJpeg(const uint8_t *yuv_data, int width, int height, int quality = 100);
void CreateJpegImage(const uint8_t *yuv_data, int width, int height, const std::string &url,
                     int quality);

} // namespace libjpeg_encoder

#endif // LIBJPEG_ENCODER_H_
