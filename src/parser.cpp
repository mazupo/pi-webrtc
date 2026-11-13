#include "parser.h"
#include "common/logging.h"

#include <algorithm>
#include <api/video/video_timing.h>
#include <api/video_codecs/scalability_mode_helper.h>
#include <boost/program_options.hpp>
#include <chrono>
#include <cstdint>
#include <linux/videodev2.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#if defined(USE_LIBCAMERA_CAPTURE)
#include <libcamera/libcamera.h>
#endif

namespace bpo = boost::program_options;

// Bitrate defaults for adaptive streams; see the --start-bitrate / --max-bitrate help.
static constexpr int kAdaptiveStartBitrateKbps = 1000;
static constexpr int kAdaptiveMinMaxBitrateKbps = 2500;
static constexpr double kAdaptiveBitsPerPixel = 0.08;
// Jetson NVENC encodes a still scene as skip frames below max(0.013 bpp, 20 kbit) per frame.
// With adaptive scaling, JetsonVideoEncoder's resolution gears set the floor instead.
static constexpr double kJetsonMinBitsPerPixel = 0.013;
static constexpr double kJetsonMinFrameBits = 20000;

static const std::unordered_map<std::string, int> v4l2_fmt_table = {
    {"mjpeg", V4L2_PIX_FMT_MJPEG},
    {"h264", V4L2_PIX_FMT_H264},
    {"i420", V4L2_PIX_FMT_YUV420},
    {"yuyv", V4L2_PIX_FMT_YUYV},
};

static const std::unordered_map<std::string, int> record_type_table = {
    {"both", -1},
    {"video", RecordType::Video},
    {"snapshot", RecordType::Snapshot},
};

static const std::unordered_map<std::string, int> record_mode_table = {
    {"both", -1},
    {"background", RecordMode::Background},
    {"on-demand", RecordMode::OnDemand},
};

static const std::unordered_map<std::string, int> stream_source_table = {
    {"main", 0},
    {"sub", 1},
};

#if defined(USE_LIBCAMERA_CAPTURE)
static const std::unordered_map<std::string, int> ae_metering_table = {
    {"centre", libcamera::controls::AeMeteringModeCentreWeighted},
    {"spot", libcamera::controls::AeMeteringModeSpot},
    {"average", libcamera::controls::AeMeteringModeMatrix},
    {"matrix", libcamera::controls::AeMeteringModeMatrix},
    {"custom", libcamera::controls::AeMeteringModeCustom}};

static const std::unordered_map<std::string, int> exposure_table = {
    {"normal", libcamera::controls::AeExposureModeNormal},
    {"sport", libcamera::controls::AeExposureModeShort},
    {"short", libcamera::controls::AeExposureModeShort},
    {"long", libcamera::controls::AeExposureModeLong},
    {"custom", libcamera::controls::AeExposureModeCustom}};

static const std::unordered_map<std::string, int> awb_table = {
    {"auto", libcamera::controls::AwbModeAuto},
    {"normal", libcamera::controls::AwbModeAuto},
    {"incandescent", libcamera::controls::AwbModeIncandescent},
    {"tungsten", libcamera::controls::AwbModeTungsten},
    {"fluorescent", libcamera::controls::AwbModeFluorescent},
    {"indoor", libcamera::controls::AwbModeIndoor},
    {"daylight", libcamera::controls::AwbModeDaylight},
    {"cloudy", libcamera::controls::AwbModeCloudy},
    {"custom", libcamera::controls::AwbModeCustom}};

static const std::unordered_map<std::string, int> denoise_table = {
    {"auto", libcamera::controls::draft::NoiseReductionModeFast},
    {"off", libcamera::controls::draft::NoiseReductionModeOff},
    {"cdn_off", libcamera::controls::draft::NoiseReductionModeMinimal},
    {"cdn_fast", libcamera::controls::draft::NoiseReductionModeFast},
    {"cdn_hq", libcamera::controls::draft::NoiseReductionModeHighQuality}};

static const std::unordered_map<std::string, int> afMode_table = {
    {"default", -1},
    {"manual", libcamera::controls::AfModeEnum::AfModeManual},
    {"auto", libcamera::controls::AfModeEnum::AfModeAuto},
    {"continuous", libcamera::controls::AfModeEnum::AfModeContinuous}};

static const std::unordered_map<std::string, int> afRange_table = {
    {"normal", libcamera::controls::AfRangeNormal},
    {"macro", libcamera::controls::AfRangeMacro},
    {"full", libcamera::controls::AfRangeFull}};

static const std::unordered_map<std::string, int> afSpeed_table = {
    {"normal", libcamera::controls::AfSpeedNormal}, {"fast", libcamera::controls::AfSpeedFast}};
#endif

namespace {

// Reads a time such as "20000" or "20ms"; a bare number counts in DEFAULT units.
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

} // namespace

// Device and room IDs as the cf-sfu-worker accepts them.
static bool IsWorkerId(const std::string &id) {
    if (id.empty() || id.size() > 64 || !isalnum(static_cast<unsigned char>(id[0]))) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

static constexpr size_t kMaxCloudflareRooms = 4;

inline int ParseEnum(const std::unordered_map<std::string, int> table, const std::string &str) {
    auto it = table.find(str);
    if (it == table.end()) {
        throw std::invalid_argument("Invalid enum string: " + str);
    }
    return it->second;
}

void Parser::ParseArgs(int argc, char *argv[], AppConfig &args) {
    bpo::options_description opts("Options");

    // clang-format off
    opts.add_options()
        ("help,h", "Display the help message")
        ("camera", bpo::value<std::string>(&args.camera)->default_value(args.camera),
            "Specify the camera using V4L2, Libcamera or an RTSP URL. "
            "e.g. \"libcamera:0\" for Libcamera, \"v4l2:0\" for V4L2 at `/dev/video0`, "
            "\"rtsp://user:pass@192.168.1.10:554/stream\" for an RTSP camera.")
        ("v4l2-format", bpo::value<std::string>(&args.v4l2_format)->default_value(args.v4l2_format),
            "The input format (`i420`, `yuyv`, `mjpeg`, `h264`) of the V4L2 camera.")
        ("uid", bpo::value<std::string>(&args.uid)->default_value(args.uid),
            "The unique id to identify the device.")
        ("fps", bpo::value<int>(&args.fps)->default_value(args.fps), "Specify the camera frames per second.")
        ("width", bpo::value<int>(&args.video.width)->default_value(args.video.width), "Set camera frame width.")
        ("height", bpo::value<int>(&args.video.height)->default_value(args.video.height), "Set camera frame height.")
        ("rotation", bpo::value<int>(&args.video.rotation)->default_value(args.video.rotation),
            "Set the rotation angle of the camera (0, 90, 180, 270).")
        ("sub-width", bpo::value<int>(&args.video.sub_width)->default_value(args.video.sub_width),
            "Set sub stream frame width for AI processing, default is 0 (disabled).")
        ("sub-height", bpo::value<int>(&args.video.sub_height)->default_value(args.video.sub_height),
            "Set sub stream frame height for AI processing, default is 0 (disabled).")
        ("record-source", bpo::value<std::string>(&args.record_source)->default_value(args.record_source),
            "Which capture stream the recorder consumes: 'main' or 'sub'. "
            "'sub' needs --sub-width and --sub-height.")
        ("webrtc-source", bpo::value<std::string>(&args.webrtc_source)->default_value(args.webrtc_source),
            "Which capture stream WebRTC publishes: 'main' or 'sub'. "
            "'sub' needs --sub-width and --sub-height.")
        ("sample-rate", bpo::value<int>(&args.audio.sample_rate)->default_value(args.audio.sample_rate),
            "Set the microphone sample rate (in Hz).")
        ("no-audio", bpo::bool_switch(&args.no_audio)->default_value(args.no_audio), "Runs without audio source.")
        ("force-alsa", bpo::bool_switch(&args.force_alsa)->default_value(args.force_alsa),
            "Force using ALSA for audio capture and playout instead of PulseAudio.")
#if defined(USE_LIBCAMERA_CAPTURE)
        ("sharpness", bpo::value<float>(&args.video.controls.sharpness)->default_value(args.video.controls.sharpness),
            "Adjust the sharpness of the libcamera output in range 0.0 to 15.99")
        ("contrast", bpo::value<float>(&args.video.controls.contrast)->default_value(args.video.controls.contrast),
            "Adjust the contrast of the libcamera output in range 0.0 to 15.99")
        ("brightness", bpo::value<float>(&args.video.controls.brightness)->default_value(args.video.controls.brightness),
            "Adjust the brightness of the libcamera output in range -1.0 to 1.0")
        ("saturation", bpo::value<float>(&args.video.controls.saturation)->default_value(args.video.controls.saturation),
            "Adjust the saturation of the libcamera output in range 0.0 to 15.99")
        ("ev", bpo::value<float>(&args.video.controls.ev)->default_value(args.video.controls.ev),
            "Set the EV (exposure value compensation) in range -10.0 to 10.0")
        ("shutter", bpo::value<std::string>(&args.shutter_)->default_value(args.shutter_),
            "Set manual shutter speed in microseconds (0 = auto)")
        ("gain", bpo::value<float>(&args.video.controls.gain)->default_value(args.video.controls.gain),
            "Set manual analog gain (0 = auto)")
        ("metering", bpo::value<std::string>(&args.ae_metering)->default_value(args.ae_metering),
            "Metering mode: centre, spot, average, custom")
        ("exposure", bpo::value<std::string>(&args.exposure)->default_value(args.exposure),
            "Exposure mode: normal, sport, short, long, custom")
        ("awb", bpo::value<std::string>(&args.awb)->default_value(args.awb),
            "Awb mode: auto, incandescent, tungsten, fluorescent, indoor, daylight, cloudy, custom")
        ("awbgains", bpo::value<std::string>(&args.awbgains)->default_value(args.awbgains),
            "Custom AWB gains as comma-separated Red, Blue values. e.g. '1.2,1.5'")
        ("denoise", bpo::value<std::string>(&args.denoise)->default_value(args.denoise),
            "Denoise mode: off, cdn_off, cdn_fast, cdn_hq, auto")
        ("tuning-file", bpo::value<std::string>(&args.tuning_file)->default_value(args.tuning_file),
			"Name of camera tuning file to use, omit this option for libcamera default behaviour")
        ("autofocus-mode", bpo::value<std::string>(&args.autofocus_mode)->default_value(args.autofocus_mode),
            "Autofocus mode: default, manual, auto, continuous")
        ("autofocus-range", bpo::value<std::string>(&args.af_range)->default_value(args.af_range),
            "Autofocus range: normal, macro, full")
        ("autofocus-speed", bpo::value<std::string>(&args.af_speed)->default_value(args.af_speed),
            "Autofocus speed: normal, fast")
        ("autofocus-window", bpo::value<std::string>(&args.af_window)->default_value(args.af_window),
            "Autofocus window as x,y,width,height. e.g. '0.3,0.3,0.4,0.4'")
        ("lens-position", bpo::value<std::string>(&args.lens_position_)->default_value(args.lens_position_),
            "Set the lens to a particular focus position, \"0\" moves the lens to infinity, or \"default\" for the hyperfocal distance")
#endif
        ("record-type", bpo::value<std::string>(&args.record_type_str)->default_value(args.record_type_str),
            "Recording type: 'video' to record MP4 files, 'snapshot' to save periodic JPEG images, "
            "or 'both' to do both simultaneously.")
        ("record-mode", bpo::value<std::string>(&args.record_mode_str)->default_value(args.record_mode_str),
            "Recording mode: 'background', 'on-demand' (DataChannel controlled), or 'both'.")
        ("record-path", bpo::value<std::string>(&args.recorder.path)->default_value(args.recorder.path),
            "Set the path where background recording video files will be saved. "
            "If the value is empty or unavailable, the background recorder will not start.")
        ("record-ondemand-path", bpo::value<std::string>(&args.record_ondemand_path)->default_value(args.record_ondemand_path),
            "Path for on-demand recordings triggered via DataChannel. "
            "Defaults to ${record-path}/on-demand/ if not set.")
        ("file-duration", bpo::value<int>(&args.recorder.file_duration)->default_value(args.recorder.file_duration),
            "The duration (in seconds) of each video file, or the interval between snapshots.")
        ("jpeg-quality", bpo::value<int>(&args.recorder.jpeg_quality)->default_value(args.recorder.jpeg_quality),
            "Set the quality of the snapshot and thumbnail images in range 0 to 100.")
        ("peer-timeout", bpo::value<int>(&args.peer_timeout)->default_value(args.peer_timeout),
            "The connection timeout (in seconds) after receiving a remote offer")
        ("max-bitrate", bpo::value<int>(&args.encoder.max_bitrate)->default_value(args.encoder.max_bitrate),
            "Maximum video bitrate (kbps). 0: 0.08 bpp with adaptive scaling (~10 Mbps at 1080p60, "
            "min. 2.5 Mbps), or 2.5 Mbps with --no-adaptive.")
        ("start-bitrate", bpo::value<int>(&args.start_bitrate)->default_value(args.start_bitrate),
            "Initial bandwidth estimate (kbps). 0: 1 Mbps with adaptive scaling, or 300 kbps with "
            "--no-adaptive, but never below --min-bitrate. Below 500 kbps, sources above VGA may be "
            "downscaled permanently.")
        ("min-bitrate", bpo::value<int>(&args.min_bitrate)->default_value(args.min_bitrate),
            "Floor (in kbps) for the bandwidth estimate and the video bitrate. 0: WebRTC's default, "
            "except on Jetson with --hw-accel and --no-adaptive, where it keeps the encoder from "
            "freezing.")
        ("hw-accel", bpo::bool_switch(&args.hw_accel)->default_value(args.hw_accel),
            "Enable hardware acceleration by sharing DMA buffers between the decoder, "
            "scaler, and encoder to reduce CPU usage.")
        ("no-adaptive", bpo::bool_switch(&args.encoder.no_adaptive)->default_value(args.encoder.no_adaptive),
            "Disable WebRTC's adaptive resolution scaling. When enabled, "
            "the output resolution will remain fixed regardless of network or device conditions.")
        ("scalability-mode", bpo::value<std::string>(&args.encoder.scalability_mode)->default_value(args.encoder.scalability_mode),
            "Temporal layers for the software video encoders, e.g. L1T2 or L1T3. "
            "Not supported with --hw-accel. Empty keeps WebRTC's default (L1T1).")
        ("min-playout-delay-ms", bpo::value<int>(&args.encoder.min_playout_delay_ms)->default_value(args.encoder.min_playout_delay_ms),
            "Lower bound of the playout delay requested from the receiver, in milliseconds. "
            "Only takes effect together with --max-playout-delay-ms.")
        ("max-playout-delay-ms", bpo::value<int>(&args.encoder.max_playout_delay_ms)->default_value(args.encoder.max_playout_delay_ms),
            "Upper bound of the playout delay requested from the receiver via the playout-delay "
            "RTP header extension, in milliseconds. The receiver adapts its jitter buffer "
            "within [min, max]; 0 for both asks it to render as soon as possible. "
            "-1 (default) does not send the extension.")
        ("latency-trace", bpo::bool_switch(&args.latency_trace)->default_value(args.latency_trace),
            "Measure per-frame latency from the sensor timestamp through capture, scaling, "
            "encoding and the handoff to WebRTC, and print p50/p95/max for each stage.")
        ("latency-trace-interval", bpo::value<int>(&args.latency_trace_interval)->default_value(args.latency_trace_interval),
            "How often (in seconds) --latency-trace prints its summary.")
        ("enable-ipc", bpo::bool_switch(&args.enable_ipc)->default_value(args.enable_ipc),
            "Enable IPC over WebRTC DataChannels.")
        ("socket-path", bpo::value<std::string>(&args.socket_path)->default_value(args.socket_path),
            "Unix socket for IPC messages.")
        ("enable-gamepad", bpo::bool_switch(&args.enable_gamepad)->default_value(args.enable_gamepad),
            "Enable browser gamepad input on a Unix socket as newline-delimited JSON. "
            "Implies --enable-ipc.")
        ("gamepad-socket-path", bpo::value<std::string>(&args.gamepad_socket_path)->default_value(args.gamepad_socket_path),
            "Unix socket for gamepad input.")
        ("stun-url", bpo::value<std::string>(&args.stun_url)->default_value(args.stun_url),
            "Set the STUN server URL for WebRTC. e.g. `stun:xxx.xxx.xxx`.")
        ("turn-url", bpo::value<std::string>(&args.turn_url)->default_value(args.turn_url),
            "Set the TURN server URL for WebRTC. e.g. `turn:xxx.xxx.xxx:3478?transport=tcp`.") 
        ("turn-username", bpo::value<std::string>(&args.turn_username)->default_value(args.turn_username),
            "Set the TURN server username for WebRTC authentication.")
        ("turn-password", bpo::value<std::string>(&args.turn_password)->default_value(args.turn_password),
            "Set the TURN server password for WebRTC authentication.")
        ("use-mqtt", bpo::bool_switch(&args.use_mqtt)->default_value(args.use_mqtt),
            "Use MQTT to exchange sdp and ice candidates.")
        ("mqtt-host", bpo::value<std::string>(&args.mqtt.host)->default_value(args.mqtt.host),
            "Set the MQTT server host.")
        ("mqtt-port", bpo::value<int>(&args.mqtt.port)->default_value(args.mqtt.port), "Set the MQTT server port.")
        ("mqtt-username", bpo::value<std::string>(&args.mqtt.username)->default_value(args.mqtt.username),
            "Set the MQTT server username.")
        ("mqtt-password", bpo::value<std::string>(&args.mqtt.password)->default_value(args.mqtt.password),
            "Set the MQTT server password.")
        ("use-whep", bpo::bool_switch(&args.use_whep)->default_value(args.use_whep),
            "Use WHEP (WebRTC-HTTP Egress Protocol) to exchange SDP and ICE candidates.")
        ("whep-port", bpo::value<uint16_t>(&args.whep.port)->default_value(args.whep.port),
            "Local HTTP server port that serves WHEP signaling.")
        ("use-livekit", bpo::bool_switch(&args.use_livekit)->default_value(args.use_livekit),
            "Enables the LiveKit client to connect to a LiveKit SFU server.")
        ("livekit-url", bpo::value<std::string>(&args.livekit.url)->default_value(args.livekit.url),
            "The LiveKit server URL, e.g. `ws://127.0.0.1:7880` or `wss://your-sfu-host.example.com`. "
            "The scheme selects TLS. The port defaults to 443 for `wss` and 80 otherwise.")
        ("livekit-room", bpo::value<std::string>(&args.livekit.room)->default_value(args.livekit.room),
            "The LiveKit room name to join.")
        ("livekit-key", bpo::value<std::string>(&args.livekit.key)->default_value(args.livekit.key),
            "The LiveKit API key used to authenticate with the server.")
        ("use-cloudflare", bpo::bool_switch(&args.use_cloudflare)->default_value(args.use_cloudflare),
            "Publish to a Cloudflare Realtime SFU through a cf-sfu-worker at --api-url. The device "
            "publishes only while someone watches one of its rooms. Requires --api-url, --api-key "
            "and --cloudflare-room.")
        ("cloudflare-room", bpo::value<std::vector<std::string>>(&args.cloudflare.rooms),
            "A room this device joins on the Cloudflare worker. Viewers watch a room. Give the "
            "option once per room, up to 4 rooms. To let people watch only this device, add a "
            "room that only this device joins.")
        ("api-url", bpo::value<std::string>(&args.cloudflare.api_url)->default_value(args.cloudflare.api_url),
            "The Cloudflare worker's API URL, e.g. https://api.mazupo.com/sfu.")
        ("api-key", bpo::value<std::string>(&args.cloudflare.api_key)->default_value(args.cloudflare.api_key),
            "The worker's device public key.")
        ("config", bpo::value<std::string>()->default_value(""),
            "Path to a YAML configuration file. All CLI options can be specified as YAML keys. "
            "Command-line arguments take priority over values in the config file.");
    // clang-format on

    bpo::variables_map vm;
    try {
        // Store CLI arguments first so they take priority over config file values
        bpo::store(bpo::parse_command_line(argc, argv, opts), vm);

        // Load YAML config file if --config was provided
        const std::string config_path = vm["config"].as<std::string>();
        if (!config_path.empty()) {
            YAML::Node yaml = YAML::LoadFile(config_path);
            if (!yaml.IsMap()) {
                ERROR_PRINT("Config file '%s' must be a YAML mapping.", config_path.c_str());
                exit(1);
            }
            // Convert YAML map to boost config-file (INI-style key=value).
            // bpo::store will not overwrite keys already set by the CLI above.
            std::istringstream ini;
            std::string ini_str;
            for (auto it = yaml.begin(); it != yaml.end(); ++it) {
                if (it->second.IsScalar()) {
                    ini_str +=
                        it->first.as<std::string>() + "=" + it->second.as<std::string>() + "\n";
                } else if (it->second.IsSequence()) {
                    // A list becomes one line per item, e.g. `cloudflare-room: [lab, door]`.
                    for (const auto &item : it->second) {
                        if (item.IsScalar()) {
                            ini_str +=
                                it->first.as<std::string>() + "=" + item.as<std::string>() + "\n";
                        }
                    }
                }
            }
            ini.str(ini_str);
            // allow_unregistered=true so unknown YAML keys don't cause errors
            bpo::store(bpo::parse_config_file(ini, opts, /*allow_unregistered=*/true), vm);
        }

        bpo::notify(vm);
    } catch (const YAML::Exception &ex) {
        ERROR_PRINT("Loading config file: %s", ex.what());
        exit(1);
    } catch (const bpo::error &ex) {
        ERROR_PRINT("Parsing arguments: %s", ex.what());
        exit(1);
    }

    if (vm.count("help")) {
        std::ostringstream oss;
        oss << opts;
        printf("%s\n", oss.str().c_str());
        exit(0);
    }

    if (!args.encoder.scalability_mode.empty()) {
        if (!webrtc::ScalabilityModeStringToEnum(args.encoder.scalability_mode)) {
            ERROR_PRINT("Unknown --scalability-mode: %s", args.encoder.scalability_mode.c_str());
            exit(1);
        }
    }

    if (args.video.sub_height > 0 && args.video.sub_width > 0) {
        if (args.video.sub_width > args.video.width || args.video.sub_height > args.video.height) {
            args.video.sub_width = args.video.width;
            args.video.sub_height = args.video.height;
            INFO_PRINT("Sub stream resolution should not be larger than main stream. Set to %dx%d",
                       args.video.sub_width, args.video.sub_height);
        }
        args.video.num_streams += 1;
        args.recorder.stream_idx = ParseEnum(stream_source_table, args.record_source);
        args.live_stream_idx = ParseEnum(stream_source_table, args.webrtc_source);
    } else {
        // Validate the values even though there is nothing to select between, so a typo is
        // still reported rather than silently falling back to the main stream.
        ParseEnum(stream_source_table, args.record_source);
        ParseEnum(stream_source_table, args.webrtc_source);
        if (args.record_source != "main" || args.webrtc_source != "main") {
            INFO_PRINT("No sub-stream configured, reading both consumers from the main stream.");
        }
        args.recorder.stream_idx = 0;
        args.live_stream_idx = 0;
    }

    if (args.uid.empty()) {
        ERROR_PRINT("--uid is required.");
        exit(1);
    }

    // Normalized once here so every consumer can just concatenate a path.
    while (!args.cloudflare.api_url.empty() && args.cloudflare.api_url.back() == '/') {
        args.cloudflare.api_url.pop_back();
    }

    if (args.cloudflare.api_url.empty()) {
        if (!args.cloudflare.api_key.empty()) {
            ERROR_PRINT("--api-key needs --api-url.");
            exit(1);
        }
    } else {
        if (args.cloudflare.api_url.rfind("https://", 0) != 0 &&
            args.cloudflare.api_url.rfind("http://", 0) != 0) {
            ERROR_PRINT("--api-url must be a full url, e.g. https://api.mazupo.com/sfu.");
            exit(1);
        }
        if (args.cloudflare.api_key.empty()) {
            ERROR_PRINT("--api-key is required when --api-url is specified.");
            exit(1);
        }
    }

    if (args.use_cloudflare) {
        if (args.cloudflare.api_url.empty()) {
            ERROR_PRINT("--api-url is required when --use-cloudflare is specified.");
            exit(1);
        }
        if (!IsWorkerId(args.uid)) {
            ERROR_PRINT("With --use-cloudflare, --uid must be 1 to 64 letters, digits, '.', '_' "
                        "or '-', and start with a letter or digit.");
            exit(1);
        }
        std::vector<std::string> rooms;
        for (const auto &room : args.cloudflare.rooms) {
            if (!IsWorkerId(room)) {
                ERROR_PRINT("Invalid --cloudflare-room \"%s\": use 1 to 64 letters, digits, '.', "
                            "'_' or '-', and start with a letter or digit.",
                            room.c_str());
                exit(1);
            }
            if (std::find(rooms.begin(), rooms.end(), room) == rooms.end()) {
                rooms.push_back(room);
            }
        }
        if (rooms.empty()) {
            ERROR_PRINT("--cloudflare-room is required when --use-cloudflare is specified.");
            exit(1);
        }
        if (rooms.size() > kMaxCloudflareRooms) {
            ERROR_PRINT("A device can join up to %zu rooms; got %zu --cloudflare-room options.",
                        kMaxCloudflareRooms, rooms.size());
            exit(1);
        }
        args.cloudflare.rooms = rooms;
    }

    if (args.use_mqtt) {
        if (args.mqtt.host.empty()) {
            ERROR_PRINT("--mqtt-host is required when --use-mqtt is specified.");
            exit(1);
        }
    }

    if (args.use_livekit) {
        if (args.livekit.url.empty()) {
            ERROR_PRINT("--livekit-url is required when --use-livekit is specified.");
            exit(1);
        }
        ParseWsUrl(args.livekit);
        if (args.livekit.room.empty()) {
            ERROR_PRINT("--livekit-room is required when --use-livekit is specified.");
            exit(1);
        }
    }

    if (!args.stun_url.empty() && args.stun_url.substr(0, 4) != "stun") {
        INFO_PRINT("Stun url should not be empty and start with \"stun:\"");
        exit(1);
    }

    if (!args.turn_url.empty() && args.turn_url.substr(0, 4) != "turn") {
        INFO_PRINT("Turn url should start with \"turn:\"");
        exit(1);
    }

    if (!args.recorder.path.empty()) {
        if (args.recorder.path.front() != '/') {
            INFO_PRINT("The file path needs to start with a \"/\" character");
            exit(1);
        }
        if (args.recorder.path.back() != '/') {
            args.recorder.path += '/';
        }
    }

#if defined(USE_LIBCAMERA_CAPTURE)
    args.video.controls.sharpness = std::clamp(args.video.controls.sharpness, 0.0f, 15.99f);
    args.video.controls.contrast = std::clamp(args.video.controls.contrast, 0.0f, 15.99f);
    args.video.controls.brightness = std::clamp(args.video.controls.brightness, -1.0f, 1.0f);
    args.video.controls.saturation = std::clamp(args.video.controls.saturation, 0.0f, 15.99f);
    args.video.controls.ev = std::clamp(args.video.controls.ev, -10.0f, 10.0f);
    TimeVal<std::chrono::microseconds> shutter;
    shutter.set(args.shutter_);
    args.video.controls.shutter_us = shutter.get<std::chrono::microseconds>();
    args.video.controls.ae_metering_mode = ParseEnum(ae_metering_table, args.ae_metering);
    args.video.controls.ae_mode = ParseEnum(exposure_table, args.exposure);
    args.video.controls.awb_mode = ParseEnum(awb_table, args.awb);

    if (sscanf(args.awbgains.c_str(), "%f,%f", &args.video.controls.awb_gain_r,
               &args.video.controls.awb_gain_b) != 2) {
        throw std::runtime_error("Invalid AWB gains");
    }

    args.video.controls.denoise_mode = ParseEnum(denoise_table, args.denoise);

    if (args.tuning_file != "-") {
        setenv("LIBCAMERA_RPI_TUNING_FILE", args.tuning_file.c_str(), 1);
    }

    args.video.controls.af_mode = ParseEnum(afMode_table, args.autofocus_mode);
    args.video.controls.af_range_mode = ParseEnum(afRange_table, args.af_range);
    args.video.controls.af_speed_mode = ParseEnum(afSpeed_table, args.af_speed);

    if (sscanf(args.af_window.c_str(), "%f,%f,%f,%f", &args.video.controls.af_window_x,
               &args.video.controls.af_window_y, &args.video.controls.af_window_width,
               &args.video.controls.af_window_height) != 4) {
        args.video.controls.af_window_x = args.video.controls.af_window_y =
            args.video.controls.af_window_width = args.video.controls.af_window_height = 0;
    }

    float f = 0.0;
    if (std::istringstream(args.lens_position_) >> f) {
        args.video.controls.lens_position = f;
    } else if (args.lens_position_ == "default") {
        args.video.controls.set_default_lens_position = true;
    } else if (!args.lens_position_.empty()) {
        throw std::runtime_error("Invalid lens position: " + args.lens_position_);
    }
#endif

    if (args.enable_gamepad) {
        args.enable_ipc = true;
    }

    args.recorder.jpeg_quality = std::clamp(args.recorder.jpeg_quality, 0, 100);
    args.latency_trace_interval = std::clamp(args.latency_trace_interval, 1, 3600);
    if (args.encoder.max_playout_delay_ms >= 0) {
        args.encoder.max_playout_delay_ms =
            std::clamp(args.encoder.max_playout_delay_ms, 0,
                       static_cast<int>(webrtc::VideoPlayoutDelay::kMax.ms()));
        args.encoder.min_playout_delay_ms =
            std::clamp(args.encoder.min_playout_delay_ms, 0, args.encoder.max_playout_delay_ms);
    }

    const bool sub = args.live_stream_idx == 1;
    const double pixels = static_cast<double>(sub ? args.video.sub_width : args.video.width) *
                          (sub ? args.video.sub_height : args.video.height);

    // WebRTC's defaults lock an adaptive stream at a low resolution: below 500 kbps it scales
    // anything over VGA down before the first frame, and its max caps at 2500 whatever the fps.
    if (!args.encoder.no_adaptive && args.encoder.max_bitrate <= 0) {
        const int bpp_kbps = static_cast<int>(pixels * args.fps * kAdaptiveBitsPerPixel / 1000);
        args.encoder.max_bitrate = std::max(kAdaptiveMinMaxBitrateKbps, bpp_kbps);
    }
#if defined(USE_JETSON_HW_ENCODER)
    if (args.hw_accel && args.encoder.no_adaptive && args.min_bitrate <= 0) {
        const double frame_bits = std::max(pixels * kJetsonMinBitsPerPixel, kJetsonMinFrameBits);
        args.min_bitrate = static_cast<int>(frame_bits * args.fps / 1000);
    }
#endif
    if (!args.encoder.no_adaptive && args.start_bitrate <= 0) {
        args.start_bitrate = std::max(kAdaptiveStartBitrateKbps, args.min_bitrate);
    }

    // BitrateSettings is rejected outright unless 0 <= min <= start <= max, so an inconsistent
    // pair is pulled into range rather than silently disabling every bound.
    if (args.encoder.max_bitrate > 0) {
        args.start_bitrate = std::min(args.start_bitrate, args.encoder.max_bitrate);
        args.min_bitrate = std::min(args.min_bitrate, args.encoder.max_bitrate);
    }
    if (args.start_bitrate > 0) {
        args.min_bitrate = std::min(args.min_bitrate, args.start_bitrate);
    }
    if (args.min_bitrate > 0 || args.start_bitrate > 0 || args.encoder.max_bitrate > 0) {
        INFO_PRINT("Video bitrate: min %d, start %d, max %d kbps (0 keeps WebRTC's default).",
                   args.min_bitrate, args.start_bitrate, args.encoder.max_bitrate);
    }

    args.recorder.type = ParseEnum(record_type_table, args.record_type_str);
    args.record_mode = ParseEnum(record_mode_table, args.record_mode_str);

    // Resolve on-demand path fallback
    if (args.record_mode != RecordMode::Background && args.record_ondemand_path.empty() &&
        !args.recorder.path.empty()) {
        args.record_ondemand_path = args.recorder.path + "on-demand/";
    }
    if (!args.record_ondemand_path.empty() && args.record_ondemand_path.back() != '/') {
        args.record_ondemand_path += '/';
    }

    ParseDevice(args);
    CopySharedOptions(args);
}

void Parser::CopySharedOptions(AppConfig &args) {
    args.video.fps = args.fps;
    args.video.hw_accel = args.hw_accel;
    args.encoder.fps = args.fps;
    args.encoder.hw_accel = args.hw_accel;
    args.recorder.hw_accel = args.hw_accel;
    args.mqtt.uid = args.uid;
    args.livekit.uid = args.uid;
    args.livekit.enable_ipc = args.enable_ipc;
    args.livekit.no_audio = args.no_audio;
    args.cloudflare.uid = args.uid;
    args.cloudflare.enable_ipc = args.enable_ipc;
    args.cloudflare.no_audio = args.no_audio;
}

void Parser::ParseWsUrl(LiveKitConfig &config) {
    auto invalid = [&config](const std::string &reason) {
        ERROR_PRINT("Invalid --livekit-url \"%s\": %s", config.url.c_str(), reason.c_str());
        exit(1);
    };

    auto scheme_end = config.url.find("://");
    if (scheme_end == std::string::npos) {
        invalid("missing scheme, expected ws://, wss://, http:// or https://");
    }

    std::string scheme = config.url.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    if (scheme == "ws" || scheme == "http") {
        config.use_tls = false;
    } else if (scheme == "wss" || scheme == "https") {
        config.use_tls = true;
    } else {
        invalid("unsupported scheme \"" + scheme + "\", expected ws, wss, http or https");
    }

    std::string rest = config.url.substr(scheme_end + 3);
    auto path_start = rest.find('/');
    std::string authority = rest.substr(0, path_start);
    if (path_start != std::string::npos && rest.substr(path_start) != "/") {
        invalid("a path is not supported, drop \"" + rest.substr(path_start) + "\"");
    }

    std::string port_str;
    if (!authority.empty() && authority.front() == '[') { // IPv6 literal
        auto bracket_end = authority.find(']');
        if (bracket_end == std::string::npos) {
            invalid("unterminated IPv6 literal, expected a closing \"]\"");
        }
        config.host = authority.substr(1, bracket_end - 1);
        if (bracket_end + 1 < authority.size()) {
            if (authority[bracket_end + 1] != ':') {
                invalid("unexpected characters after the IPv6 literal");
            }
            port_str = authority.substr(bracket_end + 2);
        }
    } else {
        auto colon = authority.find(':');
        config.host = authority.substr(0, colon);
        if (colon != std::string::npos) {
            port_str = authority.substr(colon + 1);
        }
    }

    if (config.host.empty()) {
        invalid("the host is empty");
    }

    // Left at 0 when the URL omits the port, so the signaling client applies its own
    // scheme-based default.
    config.port = 0;
    if (!port_str.empty()) {
        if (port_str.find_first_not_of("0123456789") != std::string::npos) {
            invalid("\"" + port_str + "\" is not a valid port number");
        }
        // Bound the length first so std::stoul cannot throw on an overlong digit string.
        unsigned long port = port_str.size() > 5 ? 0 : std::stoul(port_str);
        if (port == 0 || port > 65535) {
            invalid("port " + port_str + " is out of range (1-65535)");
        }
        config.port = static_cast<uint16_t>(port);
    }
}

void Parser::ParseDevice(AppConfig &args) {
    args.video.camera = args.camera;
    if (args.camera.rfind("rtsp://", 0) == 0 || args.camera.rfind("rtsps://", 0) == 0) {
        args.camera_source = CameraSource::Rtsp;
        return;
    }

    size_t pos = args.camera.find(':');
    if (pos == std::string::npos) {
        throw std::runtime_error("Invalid camera string: " + args.camera +
                                 ". Expected format: libcamera:<id> or v4l2:<id>");
    }

    std::string prefix = args.camera.substr(0, pos);
    std::string id = args.camera.substr(pos + 1);

    try {
        args.video.camera_id = std::stoi(id);
    } catch (const std::exception &e) {
        throw std::runtime_error("Invalid camera ID: " + id);
    }

    if (prefix == "libcamera") {
#if defined(USE_LIBCAMERA_CAPTURE)
        args.camera_source = CameraSource::LibCamera;
        args.video.format = V4L2_PIX_FMT_YUV420;
        INFO_PRINT("Using libcamera, ID: %d", args.video.camera_id);
#elif defined(JETSON_PLATFORM)
        throw std::runtime_error("Jetson does not support libcamera. Use v4l2:<id> instead.");
#else
        throw std::runtime_error("libcamera is not supported on this platform.");
#endif

    } else if (prefix == "libargus") {
#if defined(USE_LIBARGUS_CAPTURE)
        args.camera_source = CameraSource::LibArgus;
        args.video.format = V4L2_PIX_FMT_NV12;
#elif defined(RPI_PLATFORM)
        throw std::runtime_error("Raspberry Pi does not support libargus. Use v4l2:<id> instead.");
#else
        throw std::runtime_error("libargus is not supported on this platform.");
#endif
    } else if (prefix == "v4l2") {
        args.camera_source = CameraSource::V4L2;
        args.video.format = ParseEnum(v4l2_fmt_table, args.v4l2_format);
        INFO_PRINT("Using V4L2, ID: %d", args.video.camera_id);
        INFO_PRINT("V4L2 format: %s", args.v4l2_format.c_str());

    } else {
        throw std::runtime_error("Unknown camera type: " + prefix +
                                 ". Expected 'libcamera', 'libargus', 'v4l2' or an rtsp:// URL");
    }
}
