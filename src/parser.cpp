#include "parser.h"
#include "common/logging.h"
#include "recorder/recorder_manager.h"
#include "rtc/rtc_peer.h"

#include <algorithm>
#include <boost/program_options.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <yaml-cpp/yaml.h>

#if defined(USE_LIBCAMERA_CAPTURE)
#include <libcamera/libcamera.h>
#endif

namespace bpo = boost::program_options;

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
    {"centre", libcamera::controls::MeteringCentreWeighted},
    {"spot", libcamera::controls::MeteringSpot},
    {"average", libcamera::controls::MeteringMatrix},
    {"matrix", libcamera::controls::MeteringMatrix},
    {"custom", libcamera::controls::MeteringCustom}};

static const std::unordered_map<std::string, int> exposure_table = {
    {"normal", libcamera::controls::ExposureNormal},
    {"sport", libcamera::controls::ExposureShort},
    {"short", libcamera::controls::ExposureShort},
    {"long", libcamera::controls::ExposureLong},
    {"custom", libcamera::controls::ExposureCustom}};

static const std::unordered_map<std::string, int> awb_table = {
    {"auto", libcamera::controls::AwbAuto},
    {"normal", libcamera::controls::AwbAuto},
    {"incandescent", libcamera::controls::AwbIncandescent},
    {"tungsten", libcamera::controls::AwbTungsten},
    {"fluorescent", libcamera::controls::AwbFluorescent},
    {"indoor", libcamera::controls::AwbIndoor},
    {"daylight", libcamera::controls::AwbDaylight},
    {"cloudy", libcamera::controls::AwbCloudy},
    {"custom", libcamera::controls::AwbCustom}};

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

inline int ParseEnum(const std::unordered_map<std::string, int> table, const std::string &str) {
    auto it = table.find(str);
    if (it == table.end()) {
        throw std::invalid_argument("Invalid enum string: " + str);
    }
    return it->second;
}

void Parser::ParseArgs(int argc, char *argv[], Args &args) {
    bpo::options_description opts("Options");

    // clang-format off
    opts.add_options()
        ("help,h", "Display the help message")
        ("camera", bpo::value<std::string>(&args.camera)->default_value(args.camera),
            "Specify the camera using V4L2 or Libcamera. "
            "e.g. \"libcamera:0\" for Libcamera, \"v4l2:0\" for V4L2 at `/dev/video0`.")
        ("v4l2-format", bpo::value<std::string>(&args.v4l2_format)->default_value(args.v4l2_format),
            "The input format (`i420`, `yuyv`, `mjpeg`, `h264`) of the V4L2 camera.")
        ("uid", bpo::value<std::string>(&args.uid)->default_value(args.uid),
            "The unique id to identify the device.")
        ("fps", bpo::value<int>(&args.fps)->default_value(args.fps), "Specify the camera frames per second.")
        ("width", bpo::value<int>(&args.width)->default_value(args.width), "Set camera frame width.")
        ("height", bpo::value<int>(&args.height)->default_value(args.height), "Set camera frame height.")
        ("rotation", bpo::value<int>(&args.rotation)->default_value(args.rotation),
            "Set the rotation angle of the camera (0, 90, 180, 270).")
        ("sub-width", bpo::value<int>(&args.sub_width)->default_value(args.sub_width),
            "Set sub stream frame width for AI processing, default is 0 (disabled).")
        ("sub-height", bpo::value<int>(&args.sub_height)->default_value(args.sub_height),
            "Set sub stream frame height for AI processing, default is 0 (disabled).")
        ("record-source", bpo::value<std::string>(&args.record_source)->default_value(args.record_source),
            "Which capture stream the recorder consumes: 'main' or 'sub'. "
            "'sub' needs --sub-width and --sub-height.")
        ("webrtc-source", bpo::value<std::string>(&args.webrtc_source)->default_value(args.webrtc_source),
            "Which capture stream WebRTC publishes: 'main' or 'sub'. "
            "'sub' needs --sub-width and --sub-height.")
        ("sample-rate", bpo::value<int>(&args.sample_rate)->default_value(args.sample_rate),
            "Set the audio sample rate (in Hz).")
        ("no-audio", bpo::bool_switch(&args.no_audio)->default_value(args.no_audio), "Runs without audio source.")
        ("force-alsa", bpo::bool_switch(&args.force_alsa)->default_value(args.force_alsa),
            "Force using ALSA for audio capture instead of PulseAudio.")
#if defined(USE_LIBCAMERA_CAPTURE)
        ("sharpness", bpo::value<float>(&args.sharpness)->default_value(args.sharpness),
            "Adjust the sharpness of the libcamera output in range 0.0 to 15.99")
        ("contrast", bpo::value<float>(&args.contrast)->default_value(args.contrast),
            "Adjust the contrast of the libcamera output in range 0.0 to 15.99")
        ("brightness", bpo::value<float>(&args.brightness)->default_value(args.brightness),
            "Adjust the brightness of the libcamera output in range -1.0 to 1.0")
        ("saturation", bpo::value<float>(&args.saturation)->default_value(args.saturation),
            "Adjust the saturation of the libcamera output in range 0.0 to 15.99")
        ("ev", bpo::value<float>(&args.ev)->default_value(args.ev),
            "Set the EV (exposure value compensation) in range -10.0 to 10.0")
        ("shutter", bpo::value<std::string>(&args.shutter_)->default_value(args.shutter_),
            "Set manual shutter speed in microseconds (0 = auto)")
        ("gain", bpo::value<float>(&args.gain)->default_value(args.gain),
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
        ("record-path", bpo::value<std::string>(&args.record_path)->default_value(args.record_path),
            "Set the path where background recording video files will be saved. "
            "If the value is empty or unavailable, the background recorder will not start.")
        ("record-ondemand-path", bpo::value<std::string>(&args.record_ondemand_path)->default_value(args.record_ondemand_path),
            "Path for on-demand recordings triggered via DataChannel. "
            "Defaults to ${record-path}/on-demand/ if not set.")
        ("file-duration", bpo::value<int>(&args.file_duration)->default_value(args.file_duration),
            "The duration (in seconds) of each video file, or the interval between snapshots.")
        ("jpeg-quality", bpo::value<int>(&args.jpeg_quality)->default_value(args.jpeg_quality),
            "Set the quality of the snapshot and thumbnail images in range 0 to 100.")
        ("peer-timeout", bpo::value<int>(&args.peer_timeout)->default_value(args.peer_timeout),
            "The connection timeout (in seconds) after receiving a remote offer")
        ("max-bitrate", bpo::value<int>(&args.max_bitrate)->default_value(args.max_bitrate),
            "Ceiling (in kbps) the video sender may be allocated. 0 keeps WebRTC's own default, "
            "which is derived from the resolution and is often well below what the link can carry.")
        ("start-bitrate", bpo::value<int>(&args.start_bitrate)->default_value(args.start_bitrate),
            "Initial bandwidth estimate (in kbps). 0 keeps WebRTC's default of 300, which the "
            "estimator then has to ramp up from while every frame is squeezed to fit it.")
        ("min-bitrate", bpo::value<int>(&args.min_bitrate)->default_value(args.min_bitrate),
            "Floor (in kbps) for the bandwidth estimate. 0 keeps WebRTC's default.")
        ("hw-accel", bpo::bool_switch(&args.hw_accel)->default_value(args.hw_accel),
            "Enable hardware acceleration by sharing DMA buffers between the decoder, "
            "scaler, and encoder to reduce CPU usage.")
        ("no-adaptive", bpo::bool_switch(&args.no_adaptive)->default_value(args.no_adaptive),
            "Disable WebRTC's adaptive resolution scaling. When enabled, "
            "the output resolution will remain fixed regardless of network or device conditions.")
        ("latency-trace", bpo::bool_switch(&args.latency_trace)->default_value(args.latency_trace),
            "Measure per-frame latency from the sensor timestamp through capture, scaling, "
            "encoding and the handoff to WebRTC, and print p50/p95/max for each stage.")
        ("latency-trace-interval", bpo::value<int>(&args.latency_trace_interval)->default_value(args.latency_trace_interval),
            "How often (in seconds) --latency-trace prints its summary.")
        ("enable-ipc", bpo::bool_switch(&args.enable_ipc)->default_value(args.enable_ipc),
            "Enable IPC relay over WebRTC DataChannels. Both a lossy (UDP-like) and a "
            "reliable (TCP-like) channel are opened; the client picks one per message.")
        ("socket-path", bpo::value<std::string>(&args.socket_path)->default_value(args.socket_path),
            "Specifies the Unix domain socket path used to bridge messages between "
            "the WebRTC DataChannel and local IPC applications.")
        ("enable-gamepad", bpo::bool_switch(&args.enable_gamepad)->default_value(args.enable_gamepad),
            "Serve the `gamepad` IPC endpoint on its own socket, carrying operator input as "
            "protocol.InputReport. Each payload is written with a big-endian uint32 length in "
            "front of it, because a stream socket has no message boundaries of its own. "
            "Requires --enable-ipc, which opens the data channels this arrives on.")
        ("gamepad-socket-path", bpo::value<std::string>(&args.gamepad_socket_path)->default_value(args.gamepad_socket_path),
            "Where --enable-gamepad puts its socket.")
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
        ("mqtt-host", bpo::value<std::string>(&args.mqtt_host)->default_value(args.mqtt_host),
            "Set the MQTT server host.")
        ("mqtt-port", bpo::value<int>(&args.mqtt_port)->default_value(args.mqtt_port), "Set the MQTT server port.")
        ("mqtt-username", bpo::value<std::string>(&args.mqtt_username)->default_value(args.mqtt_username),
            "Set the MQTT server username.")
        ("mqtt-password", bpo::value<std::string>(&args.mqtt_password)->default_value(args.mqtt_password),
            "Set the MQTT server password.")
        ("use-whep", bpo::bool_switch(&args.use_whep)->default_value(args.use_whep),
            "Use WHEP (WebRTC-HTTP Egress Protocol) to exchange SDP and ICE candidates.")
        ("http-port", bpo::value<uint16_t>(&args.http_port)->default_value(args.http_port),
            "Local HTTP server port to handle signaling when using WHEP.")
        ("use-livekit", bpo::bool_switch(&args.use_livekit)->default_value(args.use_livekit),
            "Enables the LiveKit client to connect to a LiveKit SFU server.")
        ("livekit-url", bpo::value<std::string>(&args.livekit_url)->default_value(args.livekit_url),
            "The LiveKit server URL, e.g. `ws://127.0.0.1:7880` or `wss://your-sfu-host.example.com`. "
            "The scheme selects TLS. The port defaults to 443 for `wss` and 80 otherwise.")
        ("livekit-room", bpo::value<std::string>(&args.livekit_room)->default_value(args.livekit_room),
            "The LiveKit room name to join.")
        ("livekit-key", bpo::value<std::string>(&args.livekit_key)->default_value(args.livekit_key),
            "The LiveKit API key used to authenticate with the server.")
        ("use-cloudflare", bpo::bool_switch(&args.use_cloudflare)->default_value(args.use_cloudflare),
            "Enables publishing to a Cloudflare Realtime SFU through the device API. Requires "
            "--api-url and --api-key; the Realtime credentials stay on the API side.")
        ("api-url", bpo::value<std::string>(&args.api_url)->default_value(args.api_url),
            "Base URL of the picamera device API, e.g. https://api.picamera.live.")
        ("api-key", bpo::value<std::string>(&args.api_key)->default_value(args.api_key),
            "Bearer token authenticating this device against --api-url.")
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
                std::cerr << "Config file '" << config_path << "' must be a YAML mapping."
                          << std::endl;
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
                }
            }
            ini.str(ini_str);
            // allow_unregistered=true so unknown YAML keys don't cause errors
            bpo::store(bpo::parse_config_file(ini, opts, /*allow_unregistered=*/true), vm);
        }

        bpo::notify(vm);
    } catch (const YAML::Exception &ex) {
        std::cerr << "Error loading config file: " << ex.what() << std::endl;
        exit(1);
    } catch (const bpo::error &ex) {
        std::cerr << "Error parsing arguments: " << ex.what() << std::endl;
        exit(1);
    }

    if (vm.count("help")) {
        std::ostringstream oss;
        oss << opts;
        INFO_PRINT("%s", oss.str().c_str());
        exit(1);
    }

    if (args.sub_height > 0 && args.sub_width > 0) {
        if (args.sub_width > args.width || args.sub_height > args.height) {
            args.sub_width = args.width;
            args.sub_height = args.height;
            INFO_PRINT("Sub stream resolution should not be larger than main stream. Set to %dx%d",
                       args.sub_width, args.sub_height);
        }
        args.num_streams += 1;
        args.record_stream_idx = ParseEnum(stream_source_table, args.record_source);
        args.live_stream_idx = ParseEnum(stream_source_table, args.webrtc_source);
    } else {
        // Validate the values even though there is nothing to select between, so a typo is
        // still reported rather than silently falling back to the main stream.
        ParseEnum(stream_source_table, args.record_source);
        ParseEnum(stream_source_table, args.webrtc_source);
        if (args.record_source != "main" || args.webrtc_source != "main") {
            INFO_PRINT("No sub-stream configured, reading both consumers from the main stream.");
        }
        args.record_stream_idx = 0;
        args.live_stream_idx = 0;
    }

    if (args.uid.empty()) {
        std::cerr << "Error: --uid is required." << std::endl;
        exit(1);
    }

    // Normalized once here so every consumer can just concatenate a path.
    while (!args.api_url.empty() && args.api_url.back() == '/') {
        args.api_url.pop_back();
    }

    if (args.api_url.empty()) {
        if (!args.api_key.empty()) {
            std::cerr << "Error: --api-key needs --api-url." << std::endl;
            exit(1);
        }
    } else {
        if (args.api_url.rfind("https://", 0) != 0 && args.api_url.rfind("http://", 0) != 0) {
            std::cerr << "Error: --api-url must be a full url, e.g. https://api.picamera.live."
                      << std::endl;
            exit(1);
        }
        if (args.api_key.empty()) {
            std::cerr << "Error: --api-key is required when --api-url is specified." << std::endl;
            exit(1);
        }
    }

    if (args.use_cloudflare && args.api_url.empty()) {
        std::cerr << "Error: --api-url is required when --use-cloudflare is specified."
                  << std::endl;
        exit(1);
    }

    if (args.use_mqtt) {
        if (args.mqtt_host.empty()) {
            std::cerr << "Error: --mqtt-host is required when --use-mqtt is specified."
                      << std::endl;
            exit(1);
        }
    }

    if (args.use_livekit) {
        if (args.livekit_url.empty()) {
            std::cerr << "Error: --livekit-url is required when --use-livekit is specified."
                      << std::endl;
            exit(1);
        }
        ParseWsUrl(args);
        if (args.livekit_room.empty()) {
            std::cerr << "Error: --livekit-room is required when --use-livekit is specified."
                      << std::endl;
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

    if (!args.record_path.empty()) {
        if (args.record_path.front() != '/') {
            INFO_PRINT("The file path needs to start with a \"/\" character");
            exit(1);
        }
        if (args.record_path.back() != '/') {
            args.record_path += '/';
        }
    }

#if defined(USE_LIBCAMERA_CAPTURE)
    args.sharpness = std::clamp(args.sharpness, 0.0f, 15.99f);
    args.contrast = std::clamp(args.contrast, 0.0f, 15.99f);
    args.brightness = std::clamp(args.brightness, -1.0f, 1.0f);
    args.saturation = std::clamp(args.saturation, 0.0f, 15.99f);
    args.ev = std::clamp(args.ev, -10.0f, 10.0f);
    args.shutter.set(args.shutter_);
    args.ae_metering_mode = ParseEnum(ae_metering_table, args.ae_metering);
    args.ae_mode = ParseEnum(exposure_table, args.exposure);
    args.awb_mode = ParseEnum(awb_table, args.awb);

    if (sscanf(args.awbgains.c_str(), "%f,%f", &args.awb_gain_r, &args.awb_gain_b) != 2) {
        throw std::runtime_error("Invalid AWB gains");
    }

    args.denoise_mode = ParseEnum(denoise_table, args.denoise);

    if (args.tuning_file != "-") {
        setenv("LIBCAMERA_RPI_TUNING_FILE", args.tuning_file.c_str(), 1);
    }

    args.af_mode = ParseEnum(afMode_table, args.autofocus_mode);
    args.af_range_mode = ParseEnum(afRange_table, args.af_range);
    args.af_speed_mode = ParseEnum(afSpeed_table, args.af_speed);

    if (sscanf(args.af_window.c_str(), "%f,%f,%f,%f", &args.af_window_x, &args.af_window_y,
               &args.af_window_width, &args.af_window_height) != 4) {
        args.af_window_x = args.af_window_y = args.af_window_width = args.af_window_height = 0;
    }

    float f = 0.0;
    if (std::istringstream(args.lens_position_) >> f) {
        args.lens_position = f;
    } else if (args.lens_position_ == "default") {
        args.set_default_lens_position = true;
    } else if (!args.lens_position_.empty()) {
        throw std::runtime_error("Invalid lens position: " + args.lens_position_);
    }
#endif

    args.jpeg_quality = std::clamp(args.jpeg_quality, 0, 100);
    args.latency_trace_interval = std::clamp(args.latency_trace_interval, 1, 3600);

    // BitrateSettings is rejected outright unless 0 <= min <= start <= max, so an inconsistent
    // pair is pulled into range rather than silently disabling every bound.
    if (args.max_bitrate > 0) {
        args.start_bitrate = std::min(args.start_bitrate, args.max_bitrate);
        args.min_bitrate = std::min(args.min_bitrate, args.max_bitrate);
    }
    if (args.start_bitrate > 0) {
        args.min_bitrate = std::min(args.min_bitrate, args.start_bitrate);
    }

    args.record_type = ParseEnum(record_type_table, args.record_type_str);
    args.record_mode = ParseEnum(record_mode_table, args.record_mode_str);

    // Resolve on-demand path fallback
    if (args.record_mode != RecordMode::Background && args.record_ondemand_path.empty() &&
        !args.record_path.empty()) {
        args.record_ondemand_path = args.record_path + "on-demand/";
    }
    if (!args.record_ondemand_path.empty() && args.record_ondemand_path.back() != '/') {
        args.record_ondemand_path += '/';
    }

    ParseDevice(args);
}

void Parser::ParseWsUrl(Args &args) {
    auto invalid = [&args](const std::string &reason) {
        std::cerr << "Error: invalid --livekit-url \"" << args.livekit_url << "\": " << reason
                  << std::endl;
        exit(1);
    };

    auto scheme_end = args.livekit_url.find("://");
    if (scheme_end == std::string::npos) {
        invalid("missing scheme, expected ws://, wss://, http:// or https://");
    }

    std::string scheme = args.livekit_url.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    if (scheme == "ws" || scheme == "http") {
        args.livekit_use_tls = false;
    } else if (scheme == "wss" || scheme == "https") {
        args.livekit_use_tls = true;
    } else {
        invalid("unsupported scheme \"" + scheme + "\", expected ws, wss, http or https");
    }

    std::string rest = args.livekit_url.substr(scheme_end + 3);
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
        args.livekit_host = authority.substr(1, bracket_end - 1);
        if (bracket_end + 1 < authority.size()) {
            if (authority[bracket_end + 1] != ':') {
                invalid("unexpected characters after the IPv6 literal");
            }
            port_str = authority.substr(bracket_end + 2);
        }
    } else {
        auto colon = authority.find(':');
        args.livekit_host = authority.substr(0, colon);
        if (colon != std::string::npos) {
            port_str = authority.substr(colon + 1);
        }
    }

    if (args.livekit_host.empty()) {
        invalid("the host is empty");
    }

    // Left at 0 when the URL omits the port, so the signaling client applies its own
    // scheme-based default.
    args.livekit_port = 0;
    if (!port_str.empty()) {
        if (port_str.find_first_not_of("0123456789") != std::string::npos) {
            invalid("\"" + port_str + "\" is not a valid port number");
        }
        // Bound the length first so std::stoul cannot throw on an overlong digit string.
        unsigned long port = port_str.size() > 5 ? 0 : std::stoul(port_str);
        if (port == 0 || port > 65535) {
            invalid("port " + port_str + " is out of range (1-65535)");
        }
        args.livekit_port = static_cast<uint16_t>(port);
    }
}

void Parser::ParseDevice(Args &args) {
    size_t pos = args.camera.find(':');
    if (pos == std::string::npos) {
        throw std::runtime_error("Invalid camera string: " + args.camera +
                                 ". Expected format: libcamera:<id> or v4l2:<id>");
    }

    std::string prefix = args.camera.substr(0, pos);
    std::string id = args.camera.substr(pos + 1);

    try {
        args.camera_id = std::stoi(id);
    } catch (const std::exception &e) {
        throw std::runtime_error("Invalid camera ID: " + id);
    }

    if (prefix == "libcamera") {
#if defined(USE_LIBCAMERA_CAPTURE)
        args.camera_source = CameraSource::LibCamera;
        args.format = V4L2_PIX_FMT_YUV420;
        INFO_PRINT("Using libcamera, ID: %d", args.camera_id);
#elif defined(JETSON_PLATFORM)
        throw std::runtime_error("Jetson does not support libcamera. Use v4l2:<id> instead.");
#else
        throw std::runtime_error("libcamera is not supported on this platform.");
#endif

    } else if (prefix == "libargus") {
#if defined(USE_LIBARGUS_CAPTURE)
        args.camera_source = CameraSource::LibArgus;
        args.format = V4L2_PIX_FMT_NV12;
#elif defined(RPI_PLATFORM)
        throw std::runtime_error("Raspberry Pi does not support libargus. Use v4l2:<id> instead.");
#else
        throw std::runtime_error("libargus is not supported on this platform.");
#endif
    } else if (prefix == "v4l2") {
        args.camera_source = CameraSource::V4L2;
        args.format = ParseEnum(v4l2_fmt_table, args.v4l2_format);
        INFO_PRINT("Using V4L2, ID: %d", args.camera_id);
        INFO_PRINT("V4L2 format: %s", args.v4l2_format.c_str());

    } else {
        throw std::runtime_error("Unknown camera type: " + prefix +
                                 ". Expected 'libcamera', 'libargus' or 'v4l2'");
    }
}
