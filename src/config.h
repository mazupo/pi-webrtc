#ifndef CONFIG_H_
#define CONFIG_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "capturer/capture_config.h"
#include "codecs/rtc_encoder_config.h"
#include "recorder/recorder_config.h"
#include "signaling/signaling_config.h"

enum RecordMode {
    Background = 0,
    OnDemand = 1,
};

enum class CameraSource {
    LibCamera,
    LibArgus,
    V4L2,
    Rtsp,
};

template <typename DEFAULT> struct TimeVal {
    TimeVal()
        : value(0) {}

    void set(const std::string &s) {
        static const std::unordered_map<std::string, std::chrono::nanoseconds> match{
            {"min", std::chrono::minutes(1)},     {"sec", std::chrono::seconds(1)},
            {"s", std::chrono::seconds(1)},       {"ms", std::chrono::milliseconds(1)},
            {"us", std::chrono::microseconds(1)}, {"ns", std::chrono::nanoseconds(1)},
        };

        try {
            std::size_t end_pos;
            float f = std::stof(s, &end_pos);
            value = std::chrono::duration_cast<std::chrono::nanoseconds>(f * DEFAULT{1});

            for (const auto &m : match) {
                auto found = s.find(m.first, end_pos);
                if (found != end_pos || found + m.first.length() != s.length())
                    continue;
                value = std::chrono::duration_cast<std::chrono::nanoseconds>(f * m.second);
                break;
            }
        } catch (std::exception const &e) {
            throw std::runtime_error("Invalid time string provided");
        }
    }

    template <typename C = DEFAULT> int64_t get() const {
        return std::chrono::duration_cast<C>(value).count();
    }

    explicit constexpr operator bool() const { return !!value.count(); }

    std::chrono::nanoseconds value;
};

struct AppConfig {
    // shared by several modules; the parser copies them into each module config
    int fps = 30;
    bool hw_accel = false;
    std::string uid = "";
    bool enable_ipc = false;
    bool no_audio = false;

    // video input
    CameraSource camera_source = CameraSource::LibCamera;
    std::string camera = "libcamera:0";
    std::string v4l2_format = "i420";
    std::string alias = ""; // per-camera alias for recording subdirectory prefix

    // which capture stream each consumer reads from: "main" or "sub"
    std::string record_source = "main";
    std::string webrtc_source = "main";
    // derived from --webrtc-source, 0: main stream, 1: sub stream
    int live_stream_idx = 0;

    // audio
    bool force_alsa = false;

    // libcamera control options, parsed into video.controls
    std::string shutter_ = "0";
    std::string ae_metering = "centre";
    std::string exposure = "normal";
    std::string awb = "auto";
    std::string autofocus_mode = "default";
    std::string awbgains = "0,0";
    std::string denoise = "auto";
    std::string tuning_file = "-";
    std::string af_range = "normal";
    std::string af_speed = "normal";
    std::string af_window = "0,0,0,0";
    std::string lens_position_ = "";

    // recording
    std::string record_type_str = "both";
    std::string record_mode_str = "both";
    int record_mode = -1;
    std::string record_ondemand_path = "";

    // ipc
    std::string socket_path = "/tmp/pi-webrtc-ipc.sock";
    bool enable_gamepad = false;
    std::string gamepad_socket_path = "/tmp/pi-webrtc-gamepad.sock";

    // webrtc
    int peer_timeout = 60;
    // Video sender bitrate floor and start in kbps (the max is encoder.max_bitrate); 0 leaves
    // WebRTC's own default in place.
    int min_bitrate = 0;
    int start_bitrate = 0;
    bool latency_trace = false;
    int latency_trace_interval = 5;
    std::string stun_url = "stun:stun.l.google.com:19302";
    std::string turn_url = "";
    std::string turn_username = "";
    std::string turn_password = "";

    // signaling; each service's own settings are in mqtt, whep, livekit and cloudflare
    bool use_mqtt = false;
    bool use_whep = false;
    bool use_livekit = false;
    bool use_cloudflare = false;

    // settings handed to each module
    VideoCaptureConfig video;
    AudioCaptureConfig audio;
    RtcEncoderConfig encoder;
    RecorderConfig recorder;
    MqttConfig mqtt;
    WhepConfig whep;
    LiveKitConfig livekit;
    CloudflareConfig cloudflare;
};

#endif // CONFIG_H_
