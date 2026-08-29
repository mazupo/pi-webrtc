#include "signaling/cloudflare_service.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/logging.h"

namespace net = boost::asio;
namespace http = boost::beast::http;
using json = nlohmann::json;

namespace {

constexpr int kMinReconnectDelaySec = 1;
constexpr int kMaxReconnectDelaySec = 30;
constexpr int kApiTimeoutSec = 10;
// Cloudflare KV allows ~1000 writes/day on the free plan and a Workers binding write costs the
// same as a REST one, so this interval is really a write budget: 15 min is 96 writes/day/device
// before the API-side dedupe halves it, against a record that expires in an hour.
constexpr int kReportIntervalSec = 900;
constexpr int kReportRetryDelaySec = 30;
// How long the peer may stay unconnected before the session is torn down, counted from the
// moment the offer went out and, once media has flowed, from the moment the link dropped.
constexpr int kConnectTimeoutSec = 20;
constexpr int kPeerDownGraceSec = 10;
constexpr int kHealthIntervalSec = 2;
constexpr int kStatusPollSec = 30;

// Cloudflare answers 2xx with an `errorCode` in the body rather than an HTTP error status, so
// every response has to be inspected even when the request itself succeeded.
std::string ErrorOf(const json &obj) {
    if (!obj.contains("errorCode")) {
        return "";
    }
    return obj.value("errorCode", "unknown") + ": " + obj.value("errorDescription", "");
}

// The uid becomes a path segment of the device API url, and nothing constrains it to url-safe
// characters.
std::string UrlEncode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (const char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << int(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

} // namespace

std::shared_ptr<CloudflareService>
CloudflareService::Create(Args args, std::shared_ptr<Conductor> conductor, net::io_context &ioc) {
    return std::make_shared<CloudflareService>(args, conductor, ioc);
}

CloudflareService::CloudflareService(Args args, std::shared_ptr<Conductor> conductor,
                                     net::io_context &ioc)
    : conductor_(conductor),
      args_(args),
      ioc_(ioc),
      reconnect_timer_(ioc),
      health_timer_(ioc),
      status_timer_(ioc),
      report_timer_(ioc) {
    // The device API fronts the Realtime API and holds the App ID and Secret on its side.
    base_url_ = args_.api_url + "/sfu";
    report_url_ = args_.api_url + "/devices/" + UrlEncode(args_.uid) + "/session";
}

CloudflareService::~CloudflareService() { Disconnect(); }

void CloudflareService::Connect() { StartSession(); }

void CloudflareService::Disconnect() {
    stopped_ = true;
    CleanupSession();
}

void CloudflareService::StartSession() {
    if (stopped_) {
        return;
    }

    reconnect_pending_ = false;
    generation_++;
    CreateSession(generation_);
}

void CloudflareService::CreateSession(uint64_t gen) {
    INFO_PRINT("Creating a Cloudflare session through %s", base_url_.c_str());

    Request(gen, http::verb::post, "/sessions/new", "",
            [weak_self = weak_from_this(), gen](const HttpClient::Response &res) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }

                json obj = json::parse(res.body, nullptr, false);
                if (obj.is_discarded()) {
                    self->ScheduleReconnect("could not parse the /sessions/new response");
                    return;
                }
                auto error = ErrorOf(obj);
                if (!error.empty()) {
                    self->ScheduleReconnect("/sessions/new rejected, " + error);
                    return;
                }

                self->session_id_ = obj.value("sessionId", "");
                if (self->session_id_.empty()) {
                    self->ScheduleReconnect("/sessions/new returned no sessionId");
                    return;
                }

                self->CreatePeerAndOffer(gen);
            });
}

void CloudflareService::CreatePeerAndOffer(uint64_t gen) {
    if (!conductor_) {
        ERROR_PRINT("Conductor is not initialized.");
        return;
    }

    PeerConfig config;
    // Cloudflare registers channels through POST /datachannels/new and requires each one
    // pre-negotiated on the stream id that call returns, so CreateDataChannel has to be
    // handed that id rather than the RoleId default. Its data channels carry no envelope,
    // unlike LiveKit's -- hence a backend of its own rather than a shared "is an SFU" flag.
    config.backend = SignalingBackend::Cloudflare;
    config.is_publisher = true;
    // Cloudflare has no trickle-ICE endpoint, so the candidates have to be in the offer.
    config.has_candidates_in_sdp = true;
    // Cloudflare data channels are registered through /datachannels/new rather than negotiated
    // in the SDP, and the SFU channels the conductor would otherwise build speak LiveKit's
    // protobuf framing. Publishing media is all this backend does for now.
    config.no_data_channels = true;

    peer_ = conductor_->CreatePeerConnection(config);
    if (!peer_) {
        ScheduleReconnect("failed to create the peer connection");
        return;
    }

    // Conductor::AddTracks uses AddTrack, which yields sendrecv. Cloudflare treats a
    // bidirectional m-line as an explicit opt-in (`bidirectionalMediaStream`), so pin the
    // publisher's transceivers to sendonly before offering.
    for (auto &transceiver : peer_->GetPeer()->GetTransceivers()) {
        if (transceiver->sender() && transceiver->sender()->track()) {
            transceiver->SetDirectionWithError(webrtc::RtpTransceiverDirection::kSendOnly);
        }
    }

    peer_->OnLocalSdp([weak_self = weak_from_this(),
                       gen](const std::string &, const std::string &sdp, const std::string &type) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }

        // This runs on the libwebrtc signaling thread. The mids are already assigned because
        // RtcPeer::OnSuccess applies the local description before emitting the SDP, so read
        // them here and hop back onto the io_context for the HTTP call.
        auto tracks = type == "offer" ? self->CollectLocalTracks() : std::vector<LocalTrack>{};
        net::post(self->ioc_, [weak_self, gen, sdp, type, tracks]() {
            auto self = weak_self.lock();
            if (!self || self->stopped_ || gen != self->generation_) {
                return;
            }
            if (type == "offer") {
                self->PublishTracks(gen, sdp, tracks);
            } else {
                self->Renegotiate(gen, sdp);
            }
        });
    });

    // No OnLocalIce handler: there is nowhere to send trickle candidates.
    peer_->CreateOffer();
    MonitorPeer(gen);
}

std::vector<CloudflareService::LocalTrack> CloudflareService::CollectLocalTracks() const {
    std::vector<LocalTrack> tracks;
    if (!peer_ || !peer_->GetPeer()) {
        return tracks;
    }

    for (auto &transceiver : peer_->GetPeer()->GetTransceivers()) {
        auto sender = transceiver->sender();
        if (!sender || !sender->track() || !transceiver->mid()) {
            continue;
        }
        // The track id is what Conductor names the source (`video_track`, `video_track_<alias>`,
        // `audio_track`), which makes the published trackName predictable for subscribers.
        tracks.push_back({*transceiver->mid(), sender->track()->id()});
    }
    return tracks;
}

void CloudflareService::PublishTracks(uint64_t gen, const std::string &offer_sdp,
                                      const std::vector<LocalTrack> &tracks) {
    if (tracks.empty()) {
        ScheduleReconnect("the offer carried no local tracks");
        return;
    }

    json body;
    body["sessionDescription"] = {{"sdp", offer_sdp}, {"type", "offer"}};
    body["tracks"] = json::array();
    for (const auto &track : tracks) {
        body["tracks"].push_back(
            {{"location", "local"}, {"mid", track.mid}, {"trackName", track.name}});
    }

    Request(gen, http::verb::post, "/sessions/" + session_id_ + "/tracks/new", body.dump(),
            [weak_self = weak_from_this(), gen](const HttpClient::Response &res) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }

                json obj = json::parse(res.body, nullptr, false);
                if (obj.is_discarded()) {
                    self->ScheduleReconnect("could not parse the /tracks/new response");
                    return;
                }
                auto error = ErrorOf(obj);
                if (!error.empty()) {
                    self->ScheduleReconnect("/tracks/new rejected, " + error);
                    return;
                }

                if (!obj.contains("sessionDescription")) {
                    self->ScheduleReconnect("/tracks/new returned no sessionDescription");
                    return;
                }

                // Subscribers need the session id and the track names to pull this stream, and
                // Cloudflare picks the session id, so both go to the device API and to the log,
                // which is the only surface when no device API is configured. Only the tracks
                // Cloudflare accepted are worth advertising.
                INFO_PRINT("Publishing to Cloudflare session %s", self->session_id_.c_str());
                self->track_names_.clear();
                for (const auto &track : obj.value("tracks", json::array())) {
                    auto name = track.value("trackName", "");
                    const char *label = name.empty() ? "?" : name.c_str();
                    auto track_error = ErrorOf(track);
                    if (!track_error.empty()) {
                        ERROR_PRINT("Track %s was rejected, %s", label, track_error.c_str());
                        continue;
                    }
                    INFO_PRINT("  track %s (mid %s)", label, track.value("mid", "?").c_str());
                    if (!name.empty()) {
                        self->track_names_.push_back(std::move(name));
                    }
                }

                // Normally an answer. When Cloudflare needs an immediate renegotiation it
                // replies with an offer instead, and RtcPeer answers it on its own; that answer
                // comes back through OnLocalSdp and is routed to /renegotiate.
                const auto &desc = obj["sessionDescription"];
                self->peer_->SetRemoteSdp(desc.value("sdp", ""), desc.value("type", "answer"));

                self->ReportSession(gen);
                self->PollSessionStatus(gen);
            });
}

void CloudflareService::Renegotiate(uint64_t gen, const std::string &answer_sdp) {
    json body;
    body["sessionDescription"] = {{"sdp", answer_sdp}, {"type", "answer"}};

    Request(gen, http::verb::put, "/sessions/" + session_id_ + "/renegotiate", body.dump(),
            [weak_self = weak_from_this()](const HttpClient::Response &res) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }

                // A successful renegotiation carries nothing worth reading, so an empty body is
                // as good as an empty object here.
                json obj = json::parse(res.body, nullptr, false);
                if (!obj.is_discarded()) {
                    auto error = ErrorOf(obj);
                    if (!error.empty()) {
                        self->ScheduleReconnect("/renegotiate rejected, " + error);
                        return;
                    }
                }
                DEBUG_PRINT("Renegotiated session %s", self->session_id_.c_str());
            });
}

void CloudflareService::MonitorPeer(uint64_t gen) {
    health_timer_.expires_after(std::chrono::seconds(kHealthIntervalSec));
    health_timer_.async_wait(
        [weak_self = weak_from_this(), gen](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self || self->stopped_ || gen != self->generation_) {
                return;
            }

            if (!self->peer_ || self->peer_->is_expired()) {
                self->ScheduleReconnect("the peer connection expired");
                return;
            }

            if (self->peer_->is_connected()) {
                self->peer_was_connected_ = true;
                self->retry_count_ = 0;
                self->peer_down_since_ = {};
            } else {
                auto now = std::chrono::steady_clock::now();
                if (self->peer_down_since_.time_since_epoch().count() == 0) {
                    self->peer_down_since_ = now;
                }
                // Before the first connect this is the offer/answer timeout; afterwards it is a
                // grace window that lets ICE recover on its own instead of churning sessions.
                auto limit = self->peer_was_connected_ ? kPeerDownGraceSec : kConnectTimeoutSec;
                auto down =
                    std::chrono::duration_cast<std::chrono::seconds>(now - self->peer_down_since_);
                if (down.count() >= limit) {
                    self->ScheduleReconnect(self->peer_was_connected_
                                                ? "the peer connection dropped"
                                                : "the peer connection never came up");
                    return;
                }
            }

            self->MonitorPeer(gen);
        });
}

void CloudflareService::PollSessionStatus(uint64_t gen) {
    status_timer_.expires_after(std::chrono::seconds(kStatusPollSec));
    status_timer_.async_wait(
        [weak_self = weak_from_this(), gen](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self || self->stopped_ || gen != self->generation_) {
                return;
            }

            self->Request(gen, http::verb::get, "/sessions/" + self->session_id_, "",
                          [weak_self, gen](const HttpClient::Response &res) {
                              auto self = weak_self.lock();
                              if (!self) {
                                  return;
                              }

                              json obj = json::parse(res.body, nullptr, false);
                              if (!obj.is_discarded()) {
                                  for (const auto &track : obj.value("tracks", json::array())) {
                                      DEBUG_PRINT("Cloudflare track %s is %s",
                                                  track.value("trackName", "?").c_str(),
                                                  track.value("status", "?").c_str());
                                  }
                              }
                              self->PollSessionStatus(gen);
                          });
        });
}

// Cloudflare picks the session id and mints a new one on every reconnect, so a viewer has no way
// to derive it. Publishing it under the uid is what makes the uid enough to pull the stream.
//
// Deliberately not routed through Request(): that helper tears the session down on any failure.
// The registry is a convenience, and a rejected report says nothing about a stream that is
// already flowing.
void CloudflareService::ReportSession(uint64_t gen) {
    if (report_url_.empty() || session_id_.empty()) {
        return;
    }

    json body;
    body["sessionId"] = session_id_;
    body["backend"] = "cloudflare";
    body["tracks"] = track_names_;

    HttpClient::Request request;
    request.method = http::verb::put;
    request.url = report_url_;
    request.body = body.dump();
    request.bearer_token = args_.api_key;
    request.timeout_sec = kApiTimeoutSec;

    HttpClient::Send(
        ioc_, std::move(request),
        [weak_self = weak_from_this(), gen](const HttpClient::Response &res) {
            auto self = weak_self.lock();
            if (!self || self->stopped_ || gen != self->generation_) {
                return;
            }

            if (res.ok) {
                self->report_retry_count_ = 0;
                DEBUG_PRINT("Reported session %s to the device API", self->session_id_.c_str());
                self->ScheduleSessionReport(gen, kReportIntervalSec);
                return;
            }

            // A record that never arrives only costs a viewer the lookup, so back
            // off and leave the stream alone.
            int delay = kReportRetryDelaySec << std::min(self->report_retry_count_, 3);
            delay = std::min(delay, kReportIntervalSec);
            self->report_retry_count_++;
            ERROR_PRINT("Could not report the session to %s (%d), %s; retrying in %ds",
                        self->report_url_.c_str(), res.status, res.body.c_str(), delay);
            self->ScheduleSessionReport(gen, delay);
        });
}

void CloudflareService::ScheduleSessionReport(uint64_t gen, int delay_sec) {
    report_timer_.expires_after(std::chrono::seconds(delay_sec));
    report_timer_.async_wait(
        [weak_self = weak_from_this(), gen](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self || self->stopped_ || gen != self->generation_) {
                return;
            }
            self->ReportSession(gen);
        });
}

void CloudflareService::Request(uint64_t gen, http::verb method, const std::string &path,
                                const std::string &body,
                                std::function<void(const HttpClient::Response &)> on_complete) {
    HttpClient::Request request;
    request.method = method;
    request.url = base_url_ + path;
    request.body = body;
    request.bearer_token = args_.api_key;
    request.timeout_sec = kApiTimeoutSec;

    HttpClient::Send(ioc_, std::move(request),
                     [weak_self = weak_from_this(), gen, path,
                      on_complete = std::move(on_complete)](const HttpClient::Response &res) {
                         auto self = weak_self.lock();
                         if (!self || self->stopped_ || gen != self->generation_) {
                             return;
                         }
                         if (!res.ok) {
                             self->ScheduleReconnect(path + " failed with " +
                                                     std::to_string(res.status) + ", " + res.body);
                             return;
                         }
                         on_complete(res);
                     });
}

void CloudflareService::CleanupSession() {
    generation_++;

    reconnect_timer_.cancel();
    health_timer_.cancel();
    status_timer_.cancel();
    report_timer_.cancel();

    if (peer_) {
        peer_->Terminate();
        peer_ = nullptr;
    }

    session_id_.clear();
    track_names_.clear();
    report_retry_count_ = 0;
    peer_was_connected_ = false;
    peer_down_since_ = {};
}

void CloudflareService::ScheduleReconnect(const std::string &reason) {
    if (stopped_ || reconnect_pending_) {
        return;
    }
    reconnect_pending_ = true;
    CleanupSession();

    int delay = kMinReconnectDelaySec << std::min(retry_count_, 5);
    delay = std::min(delay, kMaxReconnectDelaySec);
    retry_count_++;

    auto jitter_ms = std::rand() % 500;
    ERROR_PRINT("Cloudflare session failure (%s), reconnecting in %d.%03ds", reason.c_str(), delay,
                jitter_ms);

    // A Cloudflare session is tied to one PeerConnection, so a retry always mints a new one
    // rather than trying to re-attach to the session that just went away.
    reconnect_timer_.expires_after(std::chrono::milliseconds(delay * 1000 + jitter_ms));
    reconnect_timer_.async_wait(
        [weak_self = weak_from_this()](const boost::system::error_code &ec) {
            auto self = weak_self.lock();
            if (ec || !self) {
                return;
            }
            self->StartSession();
        });
}
