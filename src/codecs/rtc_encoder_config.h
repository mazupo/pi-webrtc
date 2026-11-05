#ifndef RTC_ENCODER_CONFIG_H_
#define RTC_ENCODER_CONFIG_H_

#include <string>

// WebRTC video encoding settings, shared by the encoder factory and the encoders it creates.
struct RtcEncoderConfig {
    int fps = 30;
    int max_bitrate = 0; // kbps; 0 leaves WebRTC's own default in place
    bool no_adaptive = false;
    int min_playout_delay_ms = 0;
    int max_playout_delay_ms = -1;
    bool hw_accel = false;
    std::string scalability_mode = "";
};

#endif // RTC_ENCODER_CONFIG_H_
