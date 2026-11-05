#include "codecs/jpeg/libjpeg_encoder.h"

#include <iostream>
#include <memory>

#include <jpeglib.h>
#include <third_party/libyuv/include/libyuv.h>

#include "common/logging.h"

namespace libjpeg_encoder {

namespace {

void WriteJpegImage(JpegBuffer buffer, const std::string &url) {
    FILE *file = fopen(url.c_str(), "wb");
    if (file) {
        fwrite((uint8_t *)buffer.start.get(), 1, buffer.length, file);
        fclose(file);
        DEBUG_PRINT("JPEG data successfully written to %s", url.c_str());
    } else {
        ERROR_PRINT("Failed to open file for writing: %s", url.c_str());
    }
}

} // namespace

JpegBuffer ConvertYuvToJpeg(const uint8_t *yuv_data, int width, int height, int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    JpegBuffer jpeg_buffer;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    uint8_t *data = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&cinfo, &data, &size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_EXT_BGR;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    JSAMPROW row_pointer[1];
    int row_stride = width * 3;
    std::unique_ptr<uint8_t, decltype(&free)> rgb_guard(
        static_cast<uint8_t *>(
            malloc(static_cast<size_t>(width) * static_cast<size_t>(height) * 3)),
        free);
    uint8_t *rgb_data = rgb_guard.get();
    libyuv::I420ToRGB24(yuv_data, width, yuv_data + width * height, width / 2,
                        yuv_data + width * height + (width * height / 4), width / 2, rgb_data,
                        width * 3, width, height);

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb_data[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    jpeg_buffer.start = std::unique_ptr<uint8_t, FreeDeleter>(data);
    jpeg_buffer.length = size;

    return jpeg_buffer;
}

void CreateJpegImage(const uint8_t *yuv_data, int width, int height, const std::string &url,
                     int quality) {
    try {
        auto jpg_buffer = ConvertYuvToJpeg(yuv_data, width, height, quality);
        WriteJpegImage(std::move(jpg_buffer), url);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

} // namespace libjpeg_encoder
