#include "rtc/rtc_peer.h"
#include "common/utils.h"

#include <regex>

#include <rtc_base/thread.h>

#include "rtc/datachannel/channel_framing.h"

webrtc::scoped_refptr<RtcPeer> RtcPeer::Create(PeerConfig config) {
    return webrtc::make_ref_counted<RtcPeer>(std::move(config));
}

RtcPeer::RtcPeer(PeerConfig config)
    : id_(utils::GenerateUuid()),
      timeout_(config.timeout),
      backend_(config.backend),
      is_publisher_(config.is_publisher),
      has_candidates_in_sdp_(config.has_candidates_in_sdp) {}

RtcPeer::~RtcPeer() {
    Terminate();
    DEBUG_PRINT("peer connection (%s) was released!", id_.c_str());
}

void RtcPeer::CreateOffer() {
    if (signaling_state_ == webrtc::PeerConnectionInterface::SignalingState::kHaveLocalOffer) {
        DEBUG_PRINT("CreateOffer ignored: a local offer is already pending (%s).", id_.c_str());
        return;
    }

    peer_connection_->CreateOffer(this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

void RtcPeer::RenewSafetyFlag(webrtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> &flag) {
    if (flag) {
        flag->SetNotAlive();
    }
    flag = webrtc::PendingTaskSafetyFlag::CreateDetached();
}

void RtcPeer::Terminate() {
    is_connected_.store(false);
    is_expired_.store(true);

    on_local_sdp_fn_ = nullptr;
    on_local_ice_fn_ = nullptr;

    if (peer_timeout_safety_) {
        peer_timeout_safety_->SetNotAlive();
    }
    if (sdp_emit_safety_) {
        sdp_emit_safety_->SetNotAlive();
    }
    if (reconnect_grace_safety_) {
        reconnect_grace_safety_->SetNotAlive();
    }
    if (peer_connection_) {
        peer_connection_->Close();
        peer_connection_ = nullptr;
    }
    modified_desc_.release();
    rollback_desc_.reset();

    TerminateChannels();
}

std::string RtcPeer::id() const { return id_; }

bool RtcPeer::is_sfu_peer() const { return backend_ != SignalingBackend::Direct; }

bool RtcPeer::is_publisher() const { return is_publisher_; }

bool RtcPeer::is_connected() const { return is_connected_.load(); }

bool RtcPeer::is_expired() const { return is_expired_.load(); }

void RtcPeer::SetSink(webrtc::VideoSinkInterface<webrtc::VideoFrame> *video_sink_obj) {
    custom_video_sink_ = std::move(video_sink_obj);
}

void RtcPeer::SetPeer(webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer) {
    peer_connection_ = std::move(peer);
}

webrtc::scoped_refptr<webrtc::PeerConnectionInterface> RtcPeer::GetPeer() {
    return peer_connection_;
}

void RtcPeer::SetIpcEndpoints(std::shared_ptr<IpcEndpoints> endpoints) {
    ipc_endpoints_ = std::move(endpoints);
}

std::shared_ptr<RtcChannel> RtcPeer::CreateDataChannel(ChannelRole role, std::optional<int> id) {
    auto init = RoleInit(role);
    // Only LiveKit opens channels in-band.
    if (backend_ != SignalingBackend::LiveKit) {
        init.negotiated = true;
        init.id = id.value_or(RoleId(role));
    }
    auto label = std::string(RoleLabel(role));
    auto result = peer_connection_->CreateDataChannelOrError(label, &init);

    if (!result.ok()) {
        ERROR_PRINT("Failed to create data channel: %s", label.c_str());
        return nullptr;
    }

    return AddChannel(role, result.MoveValue());
}

std::shared_ptr<RtcChannel>
RtcPeer::AddChannel(ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> dc) {
    auto framing = ([this]() {
        switch (backend_) {
            case SignalingBackend::LiveKit:
                return LiveKitFraming::Create();
            default:
                return PlainFraming::Create();
        }
    })();

    std::shared_ptr<RtcChannel> channel;
    switch (role) {
        case ChannelRole::Command:
            channel = CommandChannel::Create(std::move(dc), std::move(framing));
            break;
        case ChannelRole::Stream:
            channel = StreamChannel::Create(std::move(dc), std::move(framing));
            break;
        case ChannelRole::Lossy:
        case ChannelRole::Reliable:
            channel = IpcChannel::Create(role, std::move(dc), std::move(framing), ipc_endpoints_);
            break;
    }

    {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[role] = channel;
    }
    DEBUG_PRINT("The %s data channel is established successfully.",
                std::string(RoleLabel(role)).c_str());

    if (role == ChannelRole::Command) {
        auto command = std::static_pointer_cast<CommandChannel>(channel);
        command->RegisterHandler(
            protocol::Request::kDisconnect, [this](const CommandChannel::Context &ctx) {
                DEBUG_PRINT("Received DISCONNECT command. Closing peer connection.");
                if (peer_connection_) {
                    peer_connection_->Close();
                    peer_connection_ = nullptr;
                }
                const auto &request = ctx.request.disconnect();
                DEBUG_PRINT(
                    "Reason: %s",
                    protocol::DisconnectRequest_DisconnectReason_Name(request.reason()).c_str());
            });
    }

    // Either channel may arrive first, so link them whenever both are present.
    auto command = GetCommandChannel();
    auto stream = GetChannel(ChannelRole::Stream);
    if (command && stream) {
        command->SetStreamChannel(std::static_pointer_cast<StreamChannel>(stream));
    }

    return channel;
}

std::shared_ptr<RtcChannel> RtcPeer::GetChannel(ChannelRole role) const {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    auto it = channels_.find(role);
    return it == channels_.end() ? nullptr : it->second;
}

std::shared_ptr<CommandChannel> RtcPeer::GetCommandChannel() const {
    return std::static_pointer_cast<CommandChannel>(GetChannel(ChannelRole::Command));
}

void RtcPeer::TerminateChannels() {
    std::map<ChannelRole, std::shared_ptr<RtcChannel>> channels;
    {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels.swap(channels_);
    }
    for (auto &[role, channel] : channels) {
        channel->Terminate();
    }
}

std::string RtcPeer::RestartIce(std::string ice_ufrag, std::string ice_pwd) {
    if (!peer_connection_ || !peer_connection_->remote_description()) {
        ERROR_PRINT("RestartIce ignored: peer connection (%s) is gone or has no remote sdp.",
                    id_.c_str());
        return "";
    }

    std::string remote_sdp;
    peer_connection_->remote_description()->ToString(&remote_sdp);

    // replace all ice_ufrag and ice_pwd in sdp.
    std::regex ufrag_regex(R"(a=ice-ufrag:([^\r\n]+))");
    std::regex pwd_regex(R"(a=ice-pwd:([^\r\n]+))");
    remote_sdp = std::regex_replace(remote_sdp, ufrag_regex, "a=ice-ufrag:" + ice_ufrag);
    remote_sdp = std::regex_replace(remote_sdp, pwd_regex, "a=ice-pwd:" + ice_pwd);
    SetRemoteSdp(remote_sdp, "offer");

    std::string local_sdp;
    peer_connection_->local_description()->ToString(&local_sdp);

    return local_sdp;
}

void RtcPeer::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
    auto previous_state = signaling_state_;
    signaling_state_ = new_state;
    auto state = webrtc::PeerConnectionInterface::AsString(new_state);
    DEBUG_PRINT("OnSignalingChange => %s", std::string(state).c_str());
    is_negotiating_.store(
        new_state == webrtc::PeerConnectionInterface::SignalingState::kHaveLocalOffer ||
        new_state == webrtc::PeerConnectionInterface::SignalingState::kHaveRemoteOffer);
    if (new_state == webrtc::PeerConnectionInterface::SignalingState::kHaveRemoteOffer) {
        // Cancel any previous timeout and schedule a new one on the signaling thread.
        RenewSafetyFlag(peer_timeout_safety_);
        webrtc::Thread::Current()->PostDelayedTask(
            webrtc::SafeTask(
                peer_timeout_safety_,
                [this]() {
                    if (peer_connection_ && !is_expired_.load() && !is_connected_.load()) {
                        DEBUG_PRINT("Connection timeout after kConnecting. Closing connection.");
                        peer_connection_->Close();
                        peer_connection_ = nullptr;
                    }
                }),
            webrtc::TimeDelta::Seconds(timeout_));
    } else if (new_state == webrtc::PeerConnectionInterface::SignalingState::kStable &&
               previous_state ==
                   webrtc::PeerConnectionInterface::SignalingState::kHaveRemoteOffer &&
               is_connected_.load() && needs_renegotiation_ && !is_sfu_peer()) {
        needs_renegotiation_ = false;
        DEBUG_PRINT("Resuming renegotiation deferred by glare rollback (%s).", id_.c_str());
        CreateOffer();
    } else if (new_state == webrtc::PeerConnectionInterface::SignalingState::kStable &&
               is_connected_.load() && !needs_renegotiation_ && !is_sfu_peer() &&
               has_candidates_in_sdp_) {
        DEBUG_PRINT("Renegotiation completed, cleaning up signaling callbacks.");
        on_local_ice_fn_ = nullptr;
        on_local_sdp_fn_ = nullptr;
    }
}

void RtcPeer::OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
    DEBUG_PRINT("On remote DataChannel => %s", channel->label().c_str());

    auto role = RoleFromLabel(channel->label());
    if (!role) {
        DEBUG_PRINT("Ignoring remote DataChannel with unknown label => %s",
                    channel->label().c_str());
        return;
    }

    AddChannel(*role, std::move(channel));
}

void RtcPeer::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
    auto state = webrtc::PeerConnectionInterface::AsString(new_state);
    DEBUG_PRINT("OnIceGatheringChange => %s", std::string(state).c_str());
}

void RtcPeer::OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
    auto state = webrtc::PeerConnectionInterface::AsString(new_state);
    DEBUG_PRINT("OnConnectionChange => %s", std::string(state).c_str());
    if (new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
        is_connected_.store(true);
        // Cancel the pending reconnect-grace.
        RenewSafetyFlag(reconnect_grace_safety_);
        if (needs_renegotiation_ &&
            signaling_state_ == webrtc::PeerConnectionInterface::SignalingState::kStable) {
            needs_renegotiation_ = false;
            DEBUG_PRINT("Triggering renegotiation for un-negotiated tracks.");
            CreateOffer();
        }
    } else if (new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed) {
        is_connected_.store(false);
        RenewSafetyFlag(reconnect_grace_safety_);
        auto *current_thread = webrtc::Thread::Current();
        DEBUG_PRINT("Arming reconnect-grace timer (thread=%p) for %d seconds (%s).",
                    (void *)current_thread, timeout_, id_.c_str());
        current_thread->PostDelayedTask(
            webrtc::SafeTask(
                reconnect_grace_safety_,
                [this]() {
                    DEBUG_PRINT("Reconnect-grace timer fired (%s): has_pc=%d, connected=%d.",
                                id_.c_str(), peer_connection_ != nullptr, is_connected_.load());
                    if (peer_connection_ && !is_connected_.load()) {
                        DEBUG_PRINT("No reconnect within %d seconds. Closing connection (%s).",
                                    timeout_, id_.c_str());
                        peer_connection_->Close();
                        peer_connection_ = nullptr;
                        is_expired_.store(true);
                    }
                }),
            webrtc::TimeDelta::Seconds(timeout_));
    } else if (new_state == webrtc::PeerConnectionInterface::PeerConnectionState::kClosed) {
        is_connected_.store(false);
        is_expired_.store(true);
    }
}

void RtcPeer::OnIceCandidate(const webrtc::IceCandidateInterface *candidate) {
    if (has_candidates_in_sdp_ && modified_desc_) {
        modified_desc_->AddCandidate(candidate);
    }

    if (on_local_ice_fn_) {
        std::string candidate_str;
        candidate->ToString(&candidate_str);
        on_local_ice_fn_(id_, candidate->sdp_mid(), candidate->sdp_mline_index(), candidate_str);
    }
}

void RtcPeer::OnRenegotiationNeeded() {
    if (is_sfu_peer()) {
        return; // SFU controls negotiation; never renegotiate from client side.
    }
    DEBUG_PRINT("OnRenegotiationNeeded for peer %s", id_.c_str());
    needs_renegotiation_ = true;
    if (is_connected_.load() &&
        signaling_state_ == webrtc::PeerConnectionInterface::SignalingState::kStable) {
        needs_renegotiation_ = false;
        DEBUG_PRINT("Triggering renegotiation (already connected and stable).");
        CreateOffer();
    }
}

void RtcPeer::OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    if (transceiver->receiver()->media_type() == webrtc::MediaType::VIDEO && custom_video_sink_) {
        auto track = transceiver->receiver()->track();
        auto remote_video_track = static_cast<webrtc::VideoTrackInterface *>(track.get());
        DEBUG_PRINT("OnTrack => custom sink(%s) is added!", track->id().c_str());
        remote_video_track->AddOrUpdateSink(custom_video_sink_, webrtc::VideoSinkWants());
    }
}

void RtcPeer::OnSuccess(webrtc::SessionDescriptionInterface *desc) {
    std::string sdp;
    desc->ToString(&sdp);

    /* An in-bound DataChannel created by the server side will not connect if the SDP is set to
     * passive. */
    // modified_sdp_ = ModifySetupAttribute(sdp, "passive");
    modified_sdp_ = sdp;
    webrtc::SdpParseError modified_desc_error_;
    // Release previous description — PeerConnection took ownership via SetLocalDescription.
    modified_desc_.release();
    modified_desc_ =
        webrtc::CreateSessionDescription(desc->GetType(), modified_sdp_, &modified_desc_error_);
    if (!modified_desc_) {
        ERROR_PRINT("Failed to create session description: %s",
                    modified_desc_error_.description.c_str());
        return;
    }

    peer_connection_->SetLocalDescription(SetSessionDescription::Create(nullptr, nullptr).get(),
                                          modified_desc_.get());

    if (has_candidates_in_sdp_) {
        EmitLocalSdp(1);
    } else {
        EmitLocalSdp();
    }
}

void RtcPeer::EmitLocalSdp(int delay_sec) {
    if (!on_local_sdp_fn_) {
        return;
    }

    // Cancel any previously scheduled SDP emit.
    RenewSafetyFlag(sdp_emit_safety_);

    auto send_sdp = [this]() {
        std::string type = webrtc::SdpTypeToString(modified_desc_->GetType());
        modified_desc_->ToString(&modified_sdp_);
        on_local_sdp_fn_(id_, modified_sdp_, type);
    };

    if (delay_sec > 0) {
        webrtc::Thread::Current()->PostDelayedTask(webrtc::SafeTask(sdp_emit_safety_,
                                                                    [this, send_sdp]() {
                                                                        send_sdp();
                                                                    }),
                                                   webrtc::TimeDelta::Seconds(delay_sec));
    } else {
        send_sdp();
    }
}

void RtcPeer::FlushPendingIce() {
    std::vector<PendingIceCandidate> candidates;
    {
        std::lock_guard<std::mutex> lock(pending_ice_mutex_);
        candidates.swap(pending_ice_candidates_);
    }
    if (candidates.empty()) {
        return;
    }
    DEBUG_PRINT("Flushing %zu buffered ICE candidates.", candidates.size());
    for (const auto &ice : candidates) {
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
            webrtc::CreateIceCandidate(ice.sdp_mid, ice.sdp_mline_index, ice.candidate, &error));
        if (!candidate.get()) {
            ERROR_PRINT("Can't parse buffered candidate: %s", error.description.c_str());
            continue;
        }
        if (!peer_connection_->AddIceCandidate(candidate.get())) {
            ERROR_PRINT("Failed to apply buffered ICE candidate!");
        }
    }
}

void RtcPeer::OnFailure(webrtc::RTCError error) {
    auto type = ToString(error.type());
    ERROR_PRINT("%s; %s", std::string(type).c_str(), error.message());
}

void RtcPeer::SetRemoteSdp(const std::string &sdp, const std::string &sdp_type) {
    if (!peer_connection_) {
        DEBUG_PRINT("SetRemoteSdp ignored: peer connection (%s) is gone.", id_.c_str());
        return;
    }
    if (!is_negotiating_.load() && sdp_type != "offer") {
        return;
    }

    std::optional<webrtc::SdpType> type_maybe = webrtc::SdpTypeFromString(sdp_type);
    if (!type_maybe) {
        ERROR_PRINT("Unknown SDP type: %s", sdp_type.c_str());
        return;
    }
    webrtc::SdpType type = *type_maybe;

    if (type == webrtc::SdpType::kOffer &&
        signaling_state_ == webrtc::PeerConnectionInterface::SignalingState::kHaveLocalOffer) {
        // Glare: we already have an outstanding local offer (e.g. triggered by
        // OnRenegotiationNeeded while adding a track) when the remote side also
        // sends an offer. libwebrtc rejects SetRemoteDescription(offer) while in
        // have-local-offer state, so roll back our local offer first and
        // re-apply the remote offer once stable again. The Pi always yields to
        // the remote offer here.
        DEBUG_PRINT(
            "Glare on peer %s: rolling back pending local offer before applying remote offer.",
            id_.c_str());
        if (!is_sfu_peer()) {
            needs_renegotiation_ = true;
        }
        rollback_desc_ = webrtc::CreateRollbackSessionDescription();
        peer_connection_->SetLocalDescription(SetSessionDescription::Create(
                                                  [this, sdp, sdp_type]() {
                                                      SetRemoteSdp(sdp, sdp_type);
                                                  },
                                                  [this](webrtc::RTCError error) {
                                                      OnFailure(error);
                                                  })
                                                  .get(),
                                              rollback_desc_.get());
        return;
    }

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
        webrtc::CreateSessionDescription(type, sdp, &error);
    if (!session_description) {
        ERROR_PRINT("Can't parse received session description message. %s",
                    error.description.c_str());
        return;
    }

    peer_connection_->SetRemoteDescription(SetSessionDescription::Create(
                                               [this]() {
                                                   FlushPendingIce();
                                               },
                                               nullptr)
                                               .get(),
                                           session_description.release());

    if (type == webrtc::SdpType::kOffer) {
        if (is_sfu_peer() && !is_publisher_) {
            for (auto &transceiver : peer_connection_->GetTransceivers()) {
                if (transceiver->media_type() == webrtc::MediaType::VIDEO) {
                    transceiver->SetDirectionWithError(webrtc::RtpTransceiverDirection::kInactive);
                }
            }
        }
        peer_connection_->CreateAnswer(this,
                                       webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
}

void RtcPeer::SetRemoteIce(const std::string &sdp_mid, int sdp_mline_index,
                           const std::string &candidate) {
    if (!peer_connection_) {
        DEBUG_PRINT("SetRemoteIce ignored: peer connection (%s) is gone.", id_.c_str());
        return;
    }

    // Only reject ICE before remote description is established (no session yet).
    if (!peer_connection_->remote_description()) {
        DEBUG_PRINT("Buffering early ICE candidate (no remote description yet).");
        std::lock_guard<std::mutex> lock(pending_ice_mutex_);
        pending_ice_candidates_.push_back({sdp_mid, sdp_mline_index, candidate});
        return;
    }

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> ice(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, &error));
    if (!ice.get()) {
        ERROR_PRINT("Can't parse received candidate message. %s", error.description.c_str());
        return;
    }

    if (!peer_connection_->AddIceCandidate(ice.get())) {
        ERROR_PRINT("Failed to apply the received candidate!");
        return;
    }
}

std::string RtcPeer::ModifySetupAttribute(const std::string &sdp, const std::string &new_setup) {
    std::string modified_sdp = sdp;
    const std::string target = "a=setup:";
    size_t pos = 0;

    while ((pos = modified_sdp.find(target, pos)) != std::string::npos) {
        size_t end_pos = modified_sdp.find("\r\n", pos);
        if (end_pos != std::string::npos) {
            modified_sdp.replace(pos, end_pos - pos, target + new_setup);
            pos = end_pos;
        } else {
            break;
        }
    }

    return modified_sdp;
}
