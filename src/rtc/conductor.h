#ifndef CONDUCTOR_H_
#define CONDUCTOR_H_

#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

#include <api/peer_connection_interface.h>
#include <rtc_base/thread.h>

#include "args.h"
#include "capturer/audio_capturer.h"
#include "capturer/video_capturer.h"
#include "recorder/recorder_manager.h"
#include "rtc/audio_device_bridge.h"
#include "rtc/rtc_peer.h"
#include "track/scale_track_source.h"

class Conductor {
  public:
    static std::shared_ptr<Conductor> Create(Args args);

    Conductor(Args args);
    ~Conductor();

    Args config() const;
    webrtc::scoped_refptr<RtcPeer> CreatePeerConnection(PeerConfig peer_config);
    std::shared_ptr<AudioCapturer> AudioSource() const;
    std::shared_ptr<VideoCapturer> VideoSource() const;
    void EnsureTracksAdded(webrtc::scoped_refptr<RtcPeer> peer);
    void SetOnDemandRecorder(std::shared_ptr<RecorderManager> recorder);

  private:
    Args args;

    void InitializePeerConnectionFactory();
    void InitializeTracks();
    void InitializeIpcServer();
    void InitializeDataChannels(webrtc::scoped_refptr<RtcPeer> peer);
    void RegisterCommandHandlers(webrtc::scoped_refptr<RtcPeer> peer);

    void
    ApplyBitrateSettings(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection);
    void AddTracks(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection);
    void TakeSnapshot(const CommandChannel::Context &ctx);
    void QueryFile(const CommandChannel::Context &ctx);
    void TransferFile(const CommandChannel::Context &ctx);
    void ControlCamera(const CommandChannel::Context &ctx);
    void ResponseQueryFile(const CommandChannel::Context &ctx, const std::string &path,
                           const protocol::VideoMode mode);
    void StartRecording(const CommandChannel::Context &ctx);
    void StopRecording(const CommandChannel::Context &ctx);

    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;

    bool use_alsa_audio_capture_ = false;
    std::shared_ptr<AudioCapturer> audio_capture_source_;
    std::shared_ptr<VideoCapturer> video_capture_source_;
    webrtc::scoped_refptr<AudioDeviceBridge> adm_;
    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
    webrtc::scoped_refptr<ScaleTrackSource> video_track_source_;

    std::shared_ptr<UnixSocketServer> ipc_server_;
    std::weak_ptr<RecorderManager> ondemand_recorder_;
};

#endif // CONDUCTOR_H_
