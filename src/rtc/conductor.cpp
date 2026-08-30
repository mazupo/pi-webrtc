#include "rtc/conductor.h"

#include <api/audio/builtin_audio_processing_builder.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_modular_peer_connection_factory.h>
#include <api/enable_media.h>
#include <api/environment/environment_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <api/transport/bitrate_settings.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_decoder_factory_template.h>
#include <api/video_codecs/video_decoder_factory_template_dav1d_adapter.h>
#include <api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h>
#include <api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h>
#include <api/video_codecs/video_decoder_factory_template_open_h264_adapter.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <rtc_base/ssl_adapter.h>

#if defined(USE_LIBCAMERA_CAPTURE)
#include "capturer/libcamera_capturer.h"
#elif defined(USE_LIBARGUS_CAPTURE)
#include "capturer/libargus_capturer.h"
#endif
#include "capturer/alsa_capturer.h"
#include "capturer/pa_capturer.h"
#include "capturer/v4l2_capturer.h"
#include "common/jpeg_util.h"
#include "common/logging.h"
#include "recorder/media_query.h"
#include "rtc/custom_video_encoder_factory.h"
#include "track/v4l2dma_track_source.h"

std::shared_ptr<Conductor> Conductor::Create(Args args) {
    auto ptr = std::make_shared<Conductor>(args);
    ptr->InitializePeerConnectionFactory();
    ptr->InitializeTracks();
    ptr->InitializeIpcEndpoints();
    return ptr;
}

Conductor::Conductor(Args args)
    : args(args) {}

Conductor::~Conductor() {
    if (ipc_endpoints_) {
        ipc_endpoints_->StopAll();
    }
    audio_track_ = nullptr;
    video_track_ = nullptr;
    video_capture_source_ = nullptr;
    peer_connection_factory_ = nullptr;
    adm_ = nullptr;

    network_thread_->Stop();
    worker_thread_->Stop();
    signaling_thread_->Stop();
    webrtc::CleanupSSL();
}

Args Conductor::config() const { return args; }

std::shared_ptr<AudioCapturer> Conductor::AudioSource() const { return audio_capture_source_; }

std::shared_ptr<VideoCapturer> Conductor::VideoSource() const { return video_capture_source_; }

void Conductor::InitializeTracks() {
    if (!audio_track_ && !args.no_audio) {
        audio_capture_source_ = ([this]() -> std::shared_ptr<AudioCapturer> {
            if (args.no_audio) {
                INFO_PRINT("Audio capture is disabled.");
                return nullptr;
            } else if (args.force_alsa) {
                INFO_PRINT("Force use Alsa capturer.");
                return AlsaCapturer::Create(args);
            } else {
                INFO_PRINT("Use PulseAudio capturer.");
                return PaCapturer::Create(args);
            }
        })();

        if (!audio_capture_source_) {
            ERROR_PRINT("Audio capturer failed to initialize; skipping audio track creation.");
        } else if (!adm_) {
            ERROR_PRINT("Audio device module is not initialized; cannot set audio capturer.");
        }

        auto options = peer_connection_factory_->CreateAudioSource(webrtc::AudioOptions());
        audio_track_ = peer_connection_factory_->CreateAudioTrack("audio_track", options.get());
    }

    if (!video_track_ && !args.camera.empty()) {
        video_capture_source_ = ([this]() -> std::shared_ptr<VideoCapturer> {
            if (args.camera_source == CameraSource::V4L2) {
                INFO_PRINT("Camera: Use v4l2 capturer.");
                return V4L2Capturer::Create(args);
            }
#if defined(USE_LIBCAMERA_CAPTURE)
            else if (args.camera_source == CameraSource::LibCamera) {
                INFO_PRINT("Camera: Use libcamera capturer.");
                return LibcameraCapturer::Create(args);
            }
#elif defined(USE_LIBARGUS_CAPTURE)
            else if (args.camera_source == CameraSource::LibArgus) {
                INFO_PRINT("Camera: Use libargus capturer.");
                return LibargusCapturer::Create(args);
            }
#endif
            ERROR_PRINT("Capturer is undefined.");
            return nullptr;
        })();

        video_track_source_ = ([this]() -> webrtc::scoped_refptr<ScaleTrackSource> {
            if (args.hw_accel) {
                return V4L2DmaTrackSource::Create(video_capture_source_);
            } else {
                return ScaleTrackSource::Create(video_capture_source_);
            }
        })();

        video_track_ =
            peer_connection_factory_->CreateVideoTrack(video_track_source_, "video_track");
    }
}

void Conductor::ApplyBitrateSettings(
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection) {
    if (args.min_bitrate <= 0 && args.start_bitrate <= 0 && args.max_bitrate <= 0) {
        return;
    }

    webrtc::BitrateSettings settings;
    if (args.min_bitrate > 0) {
        settings.min_bitrate_bps = args.min_bitrate * 1000;
    }
    if (args.start_bitrate > 0) {
        settings.start_bitrate_bps = args.start_bitrate * 1000;
    }
    if (args.max_bitrate > 0) {
        settings.max_bitrate_bps = args.max_bitrate * 1000;
    }

    auto result = peer_connection->SetBitrate(settings);
    if (!result.ok()) {
        ERROR_PRINT("Failed to set the bitrate range, %s", result.message());
    }
}

void Conductor::AddTracks(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection) {
    if (!peer_connection->GetSenders().empty()) {
        DEBUG_PRINT("Already add tracks.");
        return;
    }

    if (audio_track_) {
        auto audio_res = peer_connection->AddTrack(audio_track_, {args.uid});
        if (!audio_res.ok()) {
            ERROR_PRINT("Failed to add audio track, %s", audio_res.error().message());
        }
    }

    if (video_track_) {
        auto video_res = peer_connection->AddTrack(video_track_, {args.uid});
        if (!video_res.ok()) {
            ERROR_PRINT("Failed to add video track, %s", video_res.error().message());
        }

        auto video_sender_ = video_res.value();
        webrtc::RtpParameters parameters = video_sender_->GetParameters();
        parameters.degradation_preference = webrtc::DegradationPreference::MAINTAIN_FRAMERATE;

        if (args.max_bitrate > 0 && !parameters.encodings.empty()) {
            parameters.encodings[0].max_bitrate_bps = args.max_bitrate * 1000;
        }
        video_sender_->SetParameters(parameters);
    }
}

webrtc::scoped_refptr<RtcPeer> Conductor::CreatePeerConnection(PeerConfig config) {
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    webrtc::PeerConnectionInterface::IceServer server;
    server.uri = args.stun_url;
    config.servers.push_back(server);

    if (!args.turn_url.empty()) {
        webrtc::PeerConnectionInterface::IceServer turn_server;
        turn_server.uri = args.turn_url;
        turn_server.username = args.turn_username;
        turn_server.password = args.turn_password;
        config.servers.push_back(turn_server);
    }

    config.timeout = args.peer_timeout;
    auto peer = RtcPeer::Create(config);
    auto result = peer_connection_factory_->CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(peer.get()));

    if (!result.ok()) {
        DEBUG_PRINT("Peer connection is failed to create!");
        return nullptr;
    }

    peer->SetPeer(result.MoveValue());
    ApplyBitrateSettings(peer->GetPeer());

    if (!config.no_data_channels) {
        InitializeDataChannels(peer);
    }

    if (!config.data_channel_only) {
        AddTracks(peer->GetPeer());
    }

    DEBUG_PRINT("Peer connection(%s) is created! ", peer->id().c_str());
    return peer;
}

void Conductor::EnsureTracksAdded(webrtc::scoped_refptr<RtcPeer> peer) {
    AddTracks(peer->GetPeer());
}

void Conductor::InitializeDataChannels(webrtc::scoped_refptr<RtcPeer> peer) {
    peer->SetIpcEndpoints(ipc_endpoints_);

    if (peer->is_sfu_peer() && !peer->is_publisher()) {
        // A LiveKit subscriber peer opens via onDataChannel.
        return;
    }

    if (!peer->is_sfu_peer()) {
        peer->CreateDataChannel(ChannelRole::Command);
        peer->CreateDataChannel(ChannelRole::Stream);
        RegisterCommandHandlers(peer);
    }

    if (args.enable_ipc) {
        peer->CreateDataChannel(ChannelRole::Lossy);
        peer->CreateDataChannel(ChannelRole::Reliable);
    }
}

void Conductor::RegisterCommandHandlers(webrtc::scoped_refptr<RtcPeer> peer) {
    auto command = peer->GetCommandChannel();
    if (!command) {
        return;
    }

    using Context = CommandChannel::Context;
    command->RegisterHandler(protocol::Request::kTakeSnapshot, [this](const Context &ctx) {
        TakeSnapshot(ctx);
    });
    command->RegisterHandler(protocol::Request::kQueryFile, [this](const Context &ctx) {
        QueryFile(ctx);
    });
    command->RegisterHandler(protocol::Request::kTransferFile, [this](const Context &ctx) {
        TransferFile(ctx);
    });
    command->RegisterHandler(protocol::Request::kControlCamera, [this](const Context &ctx) {
        ControlCamera(ctx);
    });
    command->RegisterHandler(protocol::Request::kStartRecording, [this](const Context &ctx) {
        StartRecording(ctx);
    });
    command->RegisterHandler(protocol::Request::kStopRecording, [this](const Context &ctx) {
        StopRecording(ctx);
    });

    command->OnClosed([this]() {
        auto recorder = ondemand_recorder_.lock();
        if (recorder && recorder->is_recording()) {
            DEBUG_PRINT("Peer disconnected: Auto-stop on-demand recording when peer disconnects "
                        "(kFailed / kClosed)");
            recorder->Stop();
        }
    });
}

void Conductor::TakeSnapshot(const CommandChannel::Context &ctx) {
    if (!ctx.stream) {
        ERROR_PRINT("Snapshot requested on a peer without a stream channel.");
        return;
    }

    try {
        auto quality = std::clamp(ctx.request.take_snapshot().quality(), 0u, 100u);

        auto i420buff = video_capture_source_->GetI420Frame(args.live_stream_idx);
        auto jpg_buffer = jpeg_util::ConvertYuvToJpeg(
            i420buff->DataY(), video_capture_source_->width(args.live_stream_idx),
            video_capture_source_->height(args.live_stream_idx), quality);
        ctx.stream->Send(ctx.request_id, std::move(jpg_buffer));
    } catch (const std::exception &e) {
        ERROR_PRINT("%s", e.what());
    }
}

void Conductor::QueryFile(const CommandChannel::Context &ctx) {
    if (!ctx.stream) {
        ERROR_PRINT("File query requested on a peer without a stream channel.");
        return;
    }

    if (!ctx.request.has_query_file()) {
        ERROR_PRINT("Invalid metadata request");
        return;
    }

    if (args.record_path.empty()) {
        ERROR_PRINT("Recording path is not set, unable to query files.");
        return;
    }

    auto req = ctx.request.query_file();
    auto type = req.type();
    const bool is_timelapse = (req.mode() == protocol::VideoMode::TIMELAPSE);
    const std::string &parameter = req.parameter();
    auto search_dir = is_timelapse ? args.record_path + "timelapse" : args.record_path;

    DEBUG_PRINT("Received query request: mode=%s, type=%d, param=%s",
                (is_timelapse ? "TIMELAPSE" : "RECORDING"), req.type(), parameter.c_str());

    if (type == protocol::QueryFileType::LATEST_FILE || parameter.empty()) {
        auto path = media_query::FindLatestCompleteFile(search_dir, ".mp4");
        DEBUG_PRINT("LATEST: %s", path.c_str());
        ResponseQueryFile(ctx, path, req.mode());
    } else if (type == protocol::QueryFileType::BEFORE_FILE) {
        auto paths = media_query::FindOlderFiles(search_dir, parameter, 8);
        if (!paths.empty()) {
            for (auto &path : paths) {
                DEBUG_PRINT("OLDER: %s", path.c_str());
                ResponseQueryFile(ctx, path, req.mode());
            }
            return;
        }
        ResponseQueryFile(ctx, "", req.mode());
    } else if (type == protocol::QueryFileType::BEFORE_TIME) {
        auto path = media_query::FindFilesFromDatetime(search_dir, parameter);
        DEBUG_PRINT("TIME_MATCH: %s", path.c_str());
        ResponseQueryFile(ctx, path, req.mode());
    }
}

void Conductor::ResponseQueryFile(const CommandChannel::Context &ctx, const std::string &path,
                                  const protocol::VideoMode mode) {

    protocol::QueryFileResponse resp = {};

    resp.set_mode(mode);
    if (!path.empty()) {
        auto *file = resp.add_files();
        file->set_filepath(path);
        file->set_duration_sec(media_query::GetVideoDuration(path));

        std::string base64_data = media_query::GetThumbnailBase64(path);
        if (!base64_data.empty()) {
            file->set_thumbnail("data:image/jpeg;base64," + base64_data);
        }
    }

    ctx.stream->Send(ctx.request_id, resp);
}

void Conductor::TransferFile(const CommandChannel::Context &ctx) {
    if (!ctx.stream) {
        ERROR_PRINT("File transfer requested on a peer without a stream channel.");
        return;
    }

    if (args.record_path.empty()) {
        return;
    }

    if (!ctx.request.has_transfer_file()) {
        ERROR_PRINT("Invalid file transfer request");
        return;
    }

    const std::string &path = ctx.request.transfer_file().filepath();

    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            ERROR_PRINT("Unable to open file: %s", path.c_str());
            return;
        }
        ctx.stream->Send(ctx.request_id, file);
        DEBUG_PRINT("Sent Video: %s", path.c_str());
    } catch (const std::exception &e) {
        ERROR_PRINT("%s", e.what());
    }
}

void Conductor::ControlCamera(const CommandChannel::Context &ctx) {
    if (!ctx.request.has_control_camera()) {
        ERROR_PRINT("Invalid camera control request");
        return;
    }

    const auto &req = ctx.request.control_camera();
    int key = req.id();
    int value = req.value();
    DEBUG_PRINT("parse meta cmd message => %d, %d", key, value);

    try {
        if (!video_capture_source_) {
            throw std::runtime_error("No camera available.");
        }
        if (!video_capture_source_->SetControls(key, value)) {
            ERROR_PRINT("Failed to set key: %d to value: %d", key, value);
        }
    } catch (const std::exception &e) {
        ERROR_PRINT("%s", e.what());
    }
}

void Conductor::SetOnDemandRecorder(std::shared_ptr<RecorderManager> recorder) {
    ondemand_recorder_ = recorder;
}

void Conductor::StartRecording(const CommandChannel::Context &ctx) {
    auto recorder = ondemand_recorder_.lock();
    if (!recorder) {
        ERROR_PRINT("On-demand recorder is not set.");
        return;
    }
    recorder->Start();
    DEBUG_PRINT("On-demand recording started.");

    protocol::Response resp;
    auto *recording = resp.mutable_recording();
    recording->set_is_recording(true);
    recording->set_filepath(recorder->current_filepath());
    ctx.command->Send(ctx.request_id, resp);
}

void Conductor::StopRecording(const CommandChannel::Context &ctx) {
    auto recorder = ondemand_recorder_.lock();
    if (!recorder) {
        ERROR_PRINT("On-demand recorder is not set.");
        return;
    }
    const std::string filepath = recorder->current_filepath();
    recorder->Stop();
    DEBUG_PRINT("On-demand recording stopped.");

    protocol::Response resp;
    auto *recording = resp.mutable_recording();
    recording->set_is_recording(false);
    recording->set_filepath(filepath);
    ctx.command->Send(ctx.request_id, resp);
}

void Conductor::InitializePeerConnectionFactory() {
    webrtc::InitializeSSL();

    network_thread_ = webrtc::Thread::CreateWithSocketServer();
    worker_thread_ = webrtc::Thread::Create();
    signaling_thread_ = webrtc::Thread::Create();

    for (auto *thread : {network_thread_.get(), worker_thread_.get(), signaling_thread_.get()}) {
        if (!thread->Start()) {
            ERROR_PRINT("Thread start failed!");
            std::exit(EXIT_FAILURE);
        }
    }

    webrtc::Environment env = webrtc::CreateEnvironment();

    worker_thread_->BlockingCall([&]() {
        use_alsa_audio_capture_ = !args.no_audio && args.force_alsa;

        if (args.no_audio) {
            INFO_PRINT("Audio mode: dummy (no-audio)");
        } else if (use_alsa_audio_capture_) {
            INFO_PRINT("Audio mode: ALSA");
        } else {
            INFO_PRINT("Audio mode: PulseAudio");
        }

        adm_ = AudioDeviceBridge::Create();
        if (!adm_ || adm_->Init() != 0) {
            ERROR_PRINT("Failed to initialize audio device.");
            std::exit(EXIT_FAILURE);
        }
    });

    webrtc::PeerConnectionFactoryDependencies deps;
    deps.env = env;
    deps.network_thread = network_thread_.get();
    deps.worker_thread = worker_thread_.get();
    deps.signaling_thread = signaling_thread_.get();
    deps.adm = adm_;
    deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
    deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
    deps.video_encoder_factory = CreateCustomVideoEncoderFactory(args);
    deps.video_decoder_factory = std::make_unique<webrtc::VideoDecoderFactoryTemplate<
        webrtc::OpenH264DecoderTemplateAdapter, webrtc::LibvpxVp8DecoderTemplateAdapter,
        webrtc::LibvpxVp9DecoderTemplateAdapter, webrtc::Dav1dDecoderTemplateAdapter>>();
    deps.audio_processing_builder = std::make_unique<webrtc::BuiltinAudioProcessingBuilder>();

    webrtc::EnableMedia(deps);

    peer_connection_factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
}

void Conductor::InitializeIpcEndpoints() {
    if (!args.enable_ipc) {
        if (args.enable_gamepad) {
            ERROR_PRINT("--enable-gamepad needs --enable-ipc: without it there are no data "
                        "channels for gamepad input to arrive on.");
        }
        return;
    }

    ipc_endpoints_ = std::make_shared<IpcEndpoints>();
    ipc_endpoints_->Add(IpcEndpoints::kDefault, UnixSocketServer::Create(args.socket_path),
                        /*length_prefixed=*/false, /*bidirectional=*/true);
    if (args.enable_gamepad) {
        ipc_endpoints_->Add(IpcEndpoints::kGamepad,
                            UnixSocketServer::Create(args.gamepad_socket_path),
                            /*length_prefixed=*/true, /*bidirectional=*/false);
    }

    ipc_endpoints_->StartAll();
}
