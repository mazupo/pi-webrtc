#ifndef RTC_PEER_H_
#define RTC_PEER_H_

#include <atomic>
#include <mutex>
#include <vector>

#include <api/data_channel_interface.h>
#include <api/peer_connection_interface.h>
#include <api/task_queue/pending_task_safety_flag.h>
#include <api/video/video_sink_interface.h>

#include <map>

#include "args.h"
#include "common/logging.h"
#include "ipc/unix_socket_server.h"
#include "rtc/datachannel/command_channel.h"
#include "rtc/datachannel/ipc_channel.h"
#include "rtc/datachannel/rtc_channel.h"
#include "rtc/datachannel/stream_channel.h"

// Which signaling backend a peer belongs to. Everything that varies between backends is
// derived from this one value, so adding a backend is a change in one place.
enum class SignalingBackend {
    // A browser peer we negotiate with directly: MQTT or WHEP signaling.
    Direct,
    LiveKit,
    Cloudflare,
};

struct PeerConfig : public webrtc::PeerConnectionInterface::RTCConfiguration {
    int timeout = 10;
    bool is_publisher = true;
    SignalingBackend backend = SignalingBackend::Direct;
    bool has_candidates_in_sdp = false;
    bool data_channel_only = false;
    // For SFUs whose data channels are not plain SCTP streams negotiated in the SDP.
    bool no_data_channels = false;
};

class SetSessionDescription : public webrtc::SetSessionDescriptionObserver {
  public:
    typedef std::function<void()> OnSuccessFunc;
    typedef std::function<void(webrtc::RTCError)> OnFailureFunc;

    SetSessionDescription(OnSuccessFunc on_success, OnFailureFunc on_failure)
        : on_success_(std::move(on_success)),
          on_failure_(std::move(on_failure)) {}

    static webrtc::scoped_refptr<SetSessionDescription> Create(OnSuccessFunc on_success,
                                                               OnFailureFunc on_failure) {
        return webrtc::make_ref_counted<SetSessionDescription>(std::move(on_success),
                                                               std::move(on_failure));
    }

  protected:
    void OnSuccess() override {
        INFO_PRINT("=> Set sdp success!");
        auto f = std::move(on_success_);
        if (f) {
            f();
        }
    }
    void OnFailure(webrtc::RTCError error) override {
        INFO_PRINT("=> Set sdp failed: %s", error.message());
        auto f = std::move(on_failure_);
        if (f) {
            f(error);
        }
    }

    OnSuccessFunc on_success_;
    OnFailureFunc on_failure_;
};

class SignalingMessageObserver {
  public:
    using OnLocalSdpFunc = std::function<void(const std::string &peer_id, const std::string &sdp,
                                              const std::string &type)>;
    using OnLocalIceFunc =
        std::function<void(const std::string &peer_id, const std::string &sdp_mid,
                           int sdp_mline_index, const std::string &candidate)>;

    virtual void SetRemoteSdp(const std::string &sdp, const std::string &type) = 0;
    virtual void SetRemoteIce(const std::string &sdp_mid, int sdp_mline_index,
                              const std::string &candidate) = 0;

    void OnLocalSdp(OnLocalSdpFunc func) { on_local_sdp_fn_ = std::move(func); };
    void OnLocalIce(OnLocalIceFunc func) { on_local_ice_fn_ = std::move(func); };

  protected:
    OnLocalSdpFunc on_local_sdp_fn_ = nullptr;
    OnLocalIceFunc on_local_ice_fn_ = nullptr;
};

class RtcPeer : public webrtc::PeerConnectionObserver,
                public webrtc::CreateSessionDescriptionObserver,
                public SignalingMessageObserver {
  public:
    static webrtc::scoped_refptr<RtcPeer> Create(PeerConfig config);

    RtcPeer(PeerConfig config);
    ~RtcPeer();
    void CreateOffer();
    void Terminate();

    bool is_sfu_peer() const;
    bool is_publisher() const;
    bool is_connected() const;
    bool is_expired() const;
    std::string id() const;

    void SetSink(webrtc::VideoSinkInterface<webrtc::VideoFrame> *video_sink_obj);
    void SetPeer(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer);
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> GetPeer();
    void SetIpcServer(std::shared_ptr<UnixSocketServer> ipc_server);
    std::shared_ptr<RtcChannel> CreateDataChannel(ChannelRole role,
                                                  std::optional<int> id = std::nullopt);
    std::shared_ptr<RtcChannel> GetChannel(ChannelRole role) const;
    std::shared_ptr<CommandChannel> GetCommandChannel() const;
    std::string RestartIce(std::string ice_ufrag, std::string ice_pwd);

    // SignalingMessageObserver implementation.
    void SetRemoteSdp(const std::string &sdp, const std::string &type) override;
    void SetRemoteIce(const std::string &sdp_mid, int sdp_mline_index,
                      const std::string &candidate) override;

  private:
    // PeerConnectionObserver implementation.
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override;
    void
    OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
    void
    OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
    void OnIceCandidate(const webrtc::IceCandidateInterface *candidate) override;
    void OnRenegotiationNeeded() override;
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

    // CreateSessionDescriptionObserver implementation.
    void OnSuccess(webrtc::SessionDescriptionInterface *desc) override;
    void OnFailure(webrtc::RTCError error) override;

    std::string ModifySetupAttribute(const std::string &sdp, const std::string &new_setup);
    void EmitLocalSdp(int delay_sec = 0);
    void FlushPendingIce();
    void RenewSafetyFlag(webrtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> &flag);
    std::shared_ptr<RtcChannel> AddChannel(ChannelRole role,
                                           webrtc::scoped_refptr<webrtc::DataChannelInterface> dc);
    void TerminateChannels();

    struct PendingIceCandidate {
        std::string sdp_mid;
        int sdp_mline_index;
        std::string candidate;
    };
    std::vector<PendingIceCandidate> pending_ice_candidates_;
    std::mutex pending_ice_mutex_;

    int timeout_;
    std::string id_;
    SignalingBackend backend_;
    bool is_publisher_;
    bool has_candidates_in_sdp_;
    bool needs_renegotiation_ = false;
    std::atomic<bool> is_connected_ = false;
    std::atomic<bool> is_expired_ = false;
    std::atomic<bool> is_negotiating_ = false;
    webrtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> peer_timeout_safety_;
    webrtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> sdp_emit_safety_;
    webrtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> reconnect_grace_safety_;

    std::string modified_sdp_;
    webrtc::PeerConnectionInterface::SignalingState signaling_state_ =
        webrtc::PeerConnectionInterface::SignalingState::kStable;
    std::unique_ptr<webrtc::SessionDescriptionInterface> modified_desc_;
    std::unique_ptr<webrtc::SessionDescriptionInterface> rollback_desc_;

    std::shared_ptr<UnixSocketServer> ipc_server_;
    mutable std::mutex channels_mutex_;
    std::map<ChannelRole, std::shared_ptr<RtcChannel>> channels_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    webrtc::VideoSinkInterface<webrtc::VideoFrame> *custom_video_sink_;
};

#endif // RTC_PEER_H_
