#include "signaling/cloudflare_service.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

#include <nlohmann/json.hpp>

#include "common/logging.h"
#include "signaling/http_client.h"

namespace net = boost::asio;
namespace http = boost::beast::http;
using json = nlohmann::json;
using PeerState = webrtc::PeerConnectionInterface::PeerConnectionState;

namespace {

constexpr int kMinReconnectDelaySec = 1;
constexpr int kMaxReconnectDelaySec = 30;
constexpr int kApiTimeoutSec = 10;
// Not 1000/1001: those make the worker remove the device from its rooms.
constexpr int kCloseReconnecting = 3000;
constexpr int kConnectTimeoutSec = 20;
constexpr int kPeerDownGraceSec = 10;
constexpr int kHealthIntervalSec = 2;
constexpr int kPublishRetrySec = 3;
constexpr int kNominationCheckMs = 250;

// e.g. "401 key_invalid: This public key is not valid for this worker."
std::string DescribeFailure(const HttpClient::Response &res) {
    if (res.status == 0) {
        return res.body;
    }
    json obj = json::parse(res.body, nullptr, false);
    if (!obj.is_discarded() && obj.contains("error") && obj["error"].is_object()) {
        return std::to_string(res.status) + " " + obj["error"].value("code", "") + ": " +
               obj["error"].value("message", "");
    }
    return std::to_string(res.status) + " " + res.body;
}

// 128 random bits as hex. Only a connection with the same secret can replace this one.
std::string NewSecret() {
    std::random_device random;
    std::string out;
    for (int i = 0; i < 4; i++) {
        char part[9];
        std::snprintf(part, sizeof(part), "%08x", static_cast<unsigned>(random()));
        out += part;
    }
    return out;
}

std::string Join(const google::protobuf::RepeatedPtrField<std::string> &items) {
    std::string out;
    for (const auto &item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out.empty() ? "none" : out;
}

} // namespace

std::shared_ptr<CloudflareService>
CloudflareService::Create(CloudflareConfig config, std::shared_ptr<PeerFactory> peer_factory,
                          net::io_context &ioc) {
    return std::make_shared<CloudflareService>(config, peer_factory, ioc);
}

CloudflareService::CloudflareService(CloudflareConfig config,
                                     std::shared_ptr<PeerFactory> peer_factory,
                                     net::io_context &ioc)
    : peer_factory_(peer_factory),
      config_(config),
      ioc_(ioc),
      reconnect_timer_(ioc),
      health_timer_(ioc),
      retry_timer_(ioc),
      secret_(NewSecret()) {
    token_url_ = config_.api_url + "/token";
}

CloudflareService::~CloudflareService() { Disconnect(); }

void CloudflareService::Connect() { StartConnection(); }

void CloudflareService::Disconnect() {
    stopped_ = true;
    connection_++;
    reconnect_timer_.cancel();
    StopPublishing();
    publishing_ = false;
    // A normal close: the worker removes the device from its rooms right away.
    DropSocket(1000, "device stopped");
}

// -----------------------------------------------------------------------------
// Connection to the worker
// -----------------------------------------------------------------------------

void CloudflareService::StartConnection() {
    if (stopped_) {
        return;
    }
    reconnect_pending_ = false;
    RequestToken(++connection_);
}

void CloudflareService::RequestToken(uint64_t connection) {
    json body;
    body["device_id"] = config_.uid;
    body["room_ids"] = config_.rooms;
    body["can_subscribe"] = false;
    body["secret"] = secret_;
    // Keeps the stream across the reconnect.
    if (publishing_ && generation_ > 0) {
        body["generation"] = generation_;
    }

    HttpClient::Request request;
    request.method = http::verb::post;
    request.url = token_url_;
    request.body = body.dump();
    request.bearer_token = config_.api_key;
    request.timeout_sec = kApiTimeoutSec;

    HttpClient::Send(
        ioc_, std::move(request),
        [weak_self = weak_from_this(), connection](const HttpClient::Response &res) {
            auto self = weak_self.lock();
            if (!self || self->stopped_ || connection != self->connection_) {
                return;
            }

            if (!res.ok) {
                auto reason = DescribeFailure(res);
                if (res.status == 401) {
                    reason += " Check --api-key: it must be the worker's device public key.";
                }
                self->ScheduleReconnect("no access token from " + self->token_url_ + ", " + reason);
                return;
            }

            json obj = json::parse(res.body, nullptr, false);
            std::string token = obj.is_object() ? obj.value("token", "") : "";
            std::string url = obj.is_object() ? obj.value("url", "") : "";
            if (token.empty() || url.empty()) {
                self->ScheduleReconnect("the token response has no token or url");
                return;
            }
            self->OpenSocket(connection, url, token);
        });
}

void CloudflareService::OpenSocket(uint64_t connection, const std::string &url,
                                   const std::string &token) {
    auto socket = WebSocketClient::Create(ioc_);
    socket_ = socket;

    socket->OnMessage([weak_self = weak_from_this(), connection](const std::string &data) {
        auto self = weak_self.lock();
        if (self && !self->stopped_ && connection == self->connection_) {
            self->OnMessage(data);
        }
    });
    socket->OnClose(
        [weak_self = weak_from_this(), connection](int code, const std::string &reason) {
            auto self = weak_self.lock();
            if (self && !self->stopped_ && connection == self->connection_) {
                self->OnSocketClosed(code, reason);
            }
        });

    DEBUG_PRINT("Connecting to the Cloudflare worker at %s", url.c_str());
    // The token is base64url, so it needs no escaping.
    socket->Open(url + (url.find('?') == std::string::npos ? "?" : "&") + "access_token=" + token);
}

void CloudflareService::OnSocketClosed(int code, const std::string &reason) {
    socket_ = nullptr;

    if (code == protocol::CLOSE_CODE_REPLACED || code == protocol::CLOSE_CODE_ID_IN_USE) {
        // Two devices with one --uid. Or the worker still has the last run of this device, e.g.
        // after a power cut: it lets go once that connection misses its pings for 45 s.
        ERROR_PRINT("Another connection to the Cloudflare worker uses --uid=%s. Give every device "
                    "its own --uid. This device tries again in a while.",
                    config_.uid.c_str());
        StopPublishing();
        publishing_ = false;
        retry_count_ = std::max(retry_count_, 4); // 16 s, then 30 s
    }

    ScheduleReconnect(code != 0 ? "the worker closed the connection, " + std::to_string(code) +
                                      " " + reason
                                : reason);
}

void CloudflareService::ScheduleReconnect(const std::string &reason) {
    if (stopped_ || reconnect_pending_) {
        return;
    }
    reconnect_pending_ = true;
    connection_++;
    // The PeerConnection stays, so the stream goes on while reconnecting.
    DropSocket(kCloseReconnecting, "reconnecting");

    int delay = kMinReconnectDelaySec << std::min(retry_count_, 5);
    delay = std::min(delay, kMaxReconnectDelaySec);
    retry_count_++;

    auto jitter_ms = std::rand() % 500;
    ERROR_PRINT("Cloudflare worker connection failed (%s), reconnecting in %d.%03ds",
                reason.c_str(), delay, jitter_ms);

    reconnect_timer_.expires_after(std::chrono::milliseconds(delay * 1000 + jitter_ms));
    reconnect_timer_.async_wait(
        [weak_self = weak_from_this()](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self) {
                return;
            }
            self->StartConnection();
        });
}

void CloudflareService::DropSocket(int code, const std::string &reason) {
    if (!socket_) {
        return;
    }
    // Its callbacks belong to an older connection now and are ignored.
    auto socket = std::move(socket_);
    socket_ = nullptr;
    socket->Close(code, reason);
}

bool CloudflareService::Send(const protocol::DeviceRequest &request) {
    if (!socket_ || !socket_->is_open()) {
        DEBUG_PRINT("Not connected to the Cloudflare worker, a request is not sent.");
        return false;
    }
    socket_->Send(request.SerializeAsString());
    return true;
}

void CloudflareService::SendReady() {
    ready_sent_ = true;
    protocol::DeviceRequest request;
    request.mutable_ready();
    Send(request);
}

// -----------------------------------------------------------------------------
// Worker messages
// -----------------------------------------------------------------------------

void CloudflareService::OnMessage(const std::string &data) {
    protocol::DeviceResponse response;
    if (!response.ParseFromString(data)) {
        DEBUG_PRINT("Ignored a worker message that is not a DeviceResponse.");
        return;
    }

    switch (response.message_case()) {
        case protocol::DeviceResponse::kJoined:
            OnJoined(response.joined());
            break;
        case protocol::DeviceResponse::kWake:
            OnWake();
            break;
        case protocol::DeviceResponse::kAnswer:
            OnAnswer(response.answer());
            break;
        case protocol::DeviceResponse::kSleep:
            OnSleep();
            break;
        case protocol::DeviceResponse::kError:
            OnError(response.error());
            break;
        case protocol::DeviceResponse::kChannels:
            OnChannels(response.channels());
            break;
        case protocol::DeviceResponse::kControl:
            OnControl(response.control());
            break;
        case protocol::DeviceResponse::kOffer:
            OnVoicesOffer(response.offer());
            break;
        default:
            DEBUG_PRINT("Ignored worker message %d.", response.message_case());
            break;
    }
}

void CloudflareService::OnJoined(const protocol::DeviceJoined &joined) {
    retry_count_ = 0;

    ice_servers_.clear();
    for (const auto &item : joined.ice_servers()) {
        webrtc::PeerConnectionInterface::IceServer server;
        server.urls.assign(item.urls().begin(), item.urls().end());
        server.username = item.username();
        server.password = item.credential();
        if (!server.urls.empty()) {
            ice_servers_.push_back(std::move(server));
        }
    }

    INFO_PRINT("Connected to the Cloudflare worker as %s in room(s) %s (%s, generation %u)",
               config_.uid.c_str(), Join(joined.rooms()).c_str(),
               protocol::DeviceState_Name(joined.state()).c_str(), joined.generation());

    // Replies to offers on the old connection are lost.
    unanswered_ = 0;
    if (!publishing_) {
        return;
    }
    // Without the generation of an answer in the token, the worker starts over and says so here.
    if (joined.state() == protocol::DEVICE_STATE_SLEEPING) {
        INFO_PRINT("The worker ended the publication while the device was away. Stopping the "
                   "stream.");
        StopPublishing();
        publishing_ = false;
    } else if (peer_ && peer_->is_connected()) {
        // `ready` may have been lost with the old connection.
        SendReady();
    }
}

void CloudflareService::OnWake() {
    if (publishing_) {
        // The worker repeats a wake when `ready` did not come in time.
        INFO_PRINT("The worker asked to publish again. Starting over.");
    } else {
        INFO_PRINT("Someone is watching. Publishing to Cloudflare.");
    }
    publishing_ = true;
    StartPublishing();
}

void CloudflareService::OnAnswer(const protocol::PublishAnswer &answer) {
    unanswered_ = std::max(0, unanswered_ - 1);
    if (!peer_ || !offer_sent_ || unanswered_ > 0) {
        DEBUG_PRINT("Ignored the answer to an older offer (generation %u).", answer.generation());
        return;
    }
    generation_ = answer.generation();
    INFO_PRINT("Got the SFU answer (generation %u). Connecting.", generation_);
    peer_->SetRemoteSdp(answer.sdp(), "answer");
}

void CloudflareService::OnSleep() {
    if (publishing_) {
        INFO_PRINT("Nobody is watching. Stopping the Cloudflare stream.");
    }
    StopPublishing();
    publishing_ = false;
}

void CloudflareService::OnError(const protocol::SignalError &error) {
    auto where = error.request().empty() ? std::string() : " for " + error.request();
    ERROR_PRINT("The Cloudflare worker reported %s%s: %s",
                protocol::ErrorCode_Name(error.code()).c_str(), where.c_str(),
                error.message().c_str());

    // NO_DATA_CHANNELS comes after the answer: the video still goes out, only IPC is missing.
    if (error.request() != "offer" || error.code() == protocol::ERROR_CODE_NO_DATA_CHANNELS) {
        return;
    }
    unanswered_ = std::max(0, unanswered_ - 1);
    if (!offer_sent_ || unanswered_ > 0) {
        return; // the reply to an older offer
    }
    if (error.code() == protocol::ERROR_CODE_WAKE_STALE) {
        // The worker sends a new wake if someone still watches.
        StopPublishing();
        publishing_ = false;
    } else {
        RetryPublishing("the worker could not publish the offer", kPublishRetrySec);
    }
}

void CloudflareService::OnChannels(const protocol::DeviceChannels &channels) {
    if (!peer_ || generation_ == 0 || unanswered_ > 0) {
        DEBUG_PRINT("Ignored data channels for an older offer.");
        return;
    }
    for (const auto &channel : channels.channels()) {
        auto role = RoleFromLabel(channel.label());
        if (role != ChannelRole::Lossy && role != ChannelRole::Reliable) {
            continue;
        }
        // A reconnect sends the same channels again.
        if (!peer_->GetChannel(*role)) {
            peer_->CreateDataChannel(*role, static_cast<int>(channel.id()));
        }
    }
    INFO_PRINT("Opened the IPC data channels (generation %u).", generation_);
}

void CloudflareService::OnControl(const protocol::ControlChanged &control) {
    if (control.has_controller()) {
        INFO_PRINT("Cloudflare viewer %s/%s controls this device.",
                   control.controller().room().c_str(), control.controller().viewer_id().c_str());
    } else {
        INFO_PRINT("No Cloudflare viewer controls this device.");
    }
}

// -----------------------------------------------------------------------------
// Publishing
// -----------------------------------------------------------------------------

// The viewers' voices changed: the SFU's offer for this PeerConnection. The peer answers it, and
// the audio device plays what arrives, as with a viewer's microphone over MQTT.
void CloudflareService::OnVoicesOffer(const protocol::SubscribeOffer &offer) {
    if (!peer_ || generation_ == 0 || unanswered_ > 0) {
        DEBUG_PRINT("Ignored an offer for an older publication.");
        return;
    }
    INFO_PRINT("Playing the voices of %d viewer(s).", offer.tracks_size());
    peer_->SetRemoteSdp(offer.sdp(), "offer");
}

void CloudflareService::StartPublishing() {
    StopPublishing();
    auto publication = publication_;

    if (!peer_factory_) {
        ERROR_PRINT("The peer factory is not initialized.");
        return;
    }

    PeerConfig config;
    config.backend = SignalingBackend::Cloudflare;
    config.is_publisher = true;
    // The SFU is ICE-lite, so the offer needs no local candidates.
    config.has_candidates_in_sdp = false;
    config.no_data_channels = true;
    config.servers = ice_servers_;
    // The SFU may hold DTLS until the pair is nominated. Check often so that happens early.
    config.ice_check_interval_strong_connectivity = kNominationCheckMs;
    config.stable_writable_connection_ping_interval_ms = kNominationCheckMs;

    peer_ = peer_factory_->CreatePeerConnection(config);
    if (!peer_) {
        RetryPublishing("could not create the PeerConnection", kPublishRetrySec);
        return;
    }

    if (config_.enable_ipc) {
        // Any channel puts an m=application section in the offer. The worker sends the ids of
        // _lossy and _reliable after the answer.
        webrtc::DataChannelInit init;
        init.negotiated = true;
        init.id = 0;
        auto result = peer_->GetPeer()->CreateDataChannelOrError("server-events", &init);
        if (!result.ok()) {
            ERROR_PRINT("Could not add data channels to the offer: %s", result.error().message());
        }
    }

    RtcPeer *raw_peer = peer_.get();
    peer_->OnLocalSdp([weak_self = weak_from_this(), publication, raw_peer](
                          const std::string &, const std::string &sdp, const std::string &type) {
        if (type == "answer") {
            // To the SFU's offer for the viewers' voices.
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            net::post(self->ioc_, [weak_self, publication, sdp]() {
                auto self = weak_self.lock();
                if (!self || self->stopped_ || publication != self->publication_) {
                    return;
                }
                protocol::DeviceRequest request;
                request.mutable_answer()->set_sdp(sdp);
                self->Send(request);
            });
            return;
        }
        if (type != "offer") {
            return;
        }
        // On the signaling thread, after the local description set the mids.
        protocol::DeviceRequest request;
        auto *offer = request.mutable_offer();
        offer->set_sdp(sdp);
        for (const auto &transceiver : raw_peer->LocalTracks()) {
            auto *track = offer->add_tracks();
            track->set_mid(*transceiver->mid());
            track->set_track_name(transceiver->sender()->track()->id());
        }
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        net::post(self->ioc_, [weak_self, publication, request]() mutable {
            auto self = weak_self.lock();
            if (!self || self->stopped_ || publication != self->publication_) {
                return;
            }
            if (request.offer().tracks().empty()) {
                self->RetryPublishing("the offer has no tracks", kPublishRetrySec);
                return;
            }
            request.mutable_offer()->set_data_channels(self->config_.enable_ipc);
            // With --no-audio there is nothing to play them on.
            request.mutable_offer()->set_voices(!self->config_.no_audio);
            if (self->Send(request)) {
                self->unanswered_++;
                self->offer_sent_ = true;
            }
        });
    });

    peer_->OnConnectionState([weak_self = weak_from_this(), publication](PeerState state) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        net::post(self->ioc_, [weak_self, publication, state]() {
            auto self = weak_self.lock();
            if (!self || self->stopped_ || publication != self->publication_) {
                return;
            }
            self->OnPeerState(state);
        });
    });

    peer_->CreateOffer();
    MonitorPeer(publication);
}

void CloudflareService::StopPublishing() {
    publication_++;
    health_timer_.cancel();
    retry_timer_.cancel();

    if (peer_) {
        peer_->Terminate();
        peer_ = nullptr;
    }

    generation_ = 0;
    offer_sent_ = false;
    ready_sent_ = false;
    peer_was_connected_ = false;
    peer_down_since_ = {};
}

void CloudflareService::RetryPublishing(const std::string &reason, int delay_sec) {
    if (!publishing_) {
        return;
    }
    StopPublishing();
    ERROR_PRINT("Cloudflare publishing failed (%s), trying again in %ds", reason.c_str(),
                delay_sec);

    // A new offer starts a new generation. A sleep or wake meanwhile cancels the timer.
    retry_timer_.expires_after(std::chrono::seconds(delay_sec));
    retry_timer_.async_wait([weak_self = weak_from_this()](const boost::system::error_code &ec) {
        auto self = weak_self.lock();
        if (ec || !self || self->stopped_ || !self->publishing_) {
            return;
        }
        self->StartPublishing();
    });
}

void CloudflareService::OnPeerState(PeerState state) {
    if (state == PeerState::kConnected) {
        peer_was_connected_ = true;
        peer_down_since_ = {};
        if (!ready_sent_) {
            INFO_PRINT("Publishing to Cloudflare (generation %u).", generation_);
            SendReady();
        }
    } else if (state == PeerState::kFailed) {
        RetryPublishing("the PeerConnection failed", 0);
    }
}

void CloudflareService::MonitorPeer(uint64_t publication) {
    health_timer_.expires_after(std::chrono::seconds(kHealthIntervalSec));
    health_timer_.async_wait(
        [weak_self = weak_from_this(), publication](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self || self->stopped_ || publication != self->publication_) {
                return;
            }

            if (!self->peer_ || self->peer_->is_expired()) {
                self->RetryPublishing("the PeerConnection closed", kPublishRetrySec);
                return;
            }

            if (self->peer_->is_connected()) {
                self->peer_was_connected_ = true;
                self->peer_down_since_ = {};
            } else {
                auto now = std::chrono::steady_clock::now();
                if (self->peer_down_since_.time_since_epoch().count() == 0) {
                    self->peer_down_since_ = now;
                }
                // Before the first connect: the connect timeout. After: time for ICE to recover.
                auto limit = self->peer_was_connected_ ? kPeerDownGraceSec : kConnectTimeoutSec;
                auto down =
                    std::chrono::duration_cast<std::chrono::seconds>(now - self->peer_down_since_);
                if (down.count() >= limit) {
                    self->RetryPublishing(self->peer_was_connected_
                                              ? "the PeerConnection dropped"
                                              : "the PeerConnection did not connect",
                                          0);
                    return;
                }
            }

            self->MonitorPeer(publication);
        });
}
