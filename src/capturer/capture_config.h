#ifndef CAPTURE_CONFIG_H_
#define CAPTURE_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>

// Camera tuning controls; libcamera applies all of them, libargus only reads gain.
struct CameraControls {
    float sharpness = 1.0f;
    float contrast = 1.0f;
    float brightness = 0.0f;
    float saturation = 1.0f;
    float ev = 0.0f;
    int64_t shutter_us = 0; // 0 = auto
    float gain = 0.0f;
    int ae_metering_mode = 0;
    int ae_mode = 0;
    int awb_mode = 0;
    float awb_gain_r = 0.0f;
    float awb_gain_b = 0.0f;
    int denoise_mode = 0;
    int af_mode = -1;
    int af_range_mode = 0;
    int af_speed_mode = 0;
    float af_window_x = 0.0f;
    float af_window_y = 0.0f;
    float af_window_width = 0.0f;
    float af_window_height = 0.0f;
    std::optional<float> lens_position;
    bool set_default_lens_position = false;
};

struct VideoCaptureConfig {
    std::string camera = ""; // --camera as given, e.g. libcamera:0 or an RTSP URL
    uint32_t camera_id = 0;
    uint32_t format = 0; // V4L2 pixel format
    int width = 640;
    int height = 480;
    int fps = 30;
    int rotation = 0;
    int num_streams = 1;
    int sub_width = 0;
    int sub_height = 0;
    bool hw_accel = false;
    CameraControls controls;
};

struct AudioCaptureConfig {
    int sample_rate = 48000;
    int channels = 2;
};

#endif // CAPTURE_CONFIG_H_
