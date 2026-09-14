#include "signaling/whep_service.h"

#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

#include "common/logging.h"

namespace {

constexpr int kAnswerTimeoutSec = 10;

constexpr char kSdpType[] = "application/sdp";
constexpr char kTrickleIceType[] = "application/trickle-ice-sdpfrag";
constexpr char kSessionSegment[] = "sessions";

// Extract the media type without parameters and lowercase it.
std::string MediaTypeOf(const std::string &value) {
    auto type = value.substr(0, value.find(';'));
    auto begin = type.find_first_not_of(" \t");
    auto end = type.find_last_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    type = type.substr(begin, end - begin + 1);
    for (auto &c : type) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return type;
}

// ICE username fragment identifies the ICE session and serves as its strong entity-tag.
std::string EntityTagOf(const std::string &sdp) {
    static const std::regex ufrag_regex(R"(a=ice-ufrag:([^\r\n]+))");
    std::smatch match;
    if (!std::regex_search(sdp, match, ufrag_regex)) {
        return "";
    }
    return "\"" + match[1].str() + "\"";
}

std::string LocalSdpOf(const webrtc::scoped_refptr<RtcPeer> &peer) {
    auto pc = peer->GetPeer();
    if (!pc || !pc->local_description()) {
        return "";
    }
    std::string sdp;
    pc->local_description()->ToString(&sdp);
    return sdp;
}

} // namespace

WhepTarget ParseWhepTarget(const std::string &target) {
    std::vector<std::string> segments;
    std::stringstream ss(target.substr(0, target.find('?')));
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }

    WhepTarget result;
    if (segments.size() == 2 && segments[0] == kSessionSegment) {
        result.kind = WhepTarget::Kind::Session;
        result.peer_id = segments[1];
    } else if (segments.empty()) {
        result.kind = WhepTarget::Kind::Endpoint;
    } else if (segments.size() == 1 && segments[0] != kSessionSegment) {
        result.kind = WhepTarget::Kind::Endpoint;
        result.stream = segments[0];
    }
    return result;
}

std::shared_ptr<WhepService> WhepService::Create(Args args, std::shared_ptr<Conductor> conductor,
                                                 boost::asio::io_context &ioc) {
    return std::make_shared<WhepService>(args, conductor, ioc);
}

WhepService::WhepService(Args args, std::shared_ptr<Conductor> conductor,
                         boost::asio::io_context &ioc)
    : conductor_(conductor),
      port_(args.http_port),
      acceptor_({ioc, {boost::asio::ip::address_v6::any(), port_}}) {}

WhepService::~WhepService() { Disconnect(); }

void WhepService::Connect() {
    INFO_PRINT("WHEP server is running on http://*:%d", port_);
    peer_registry_.Start();
    AcceptConnection();
}

void WhepService::Disconnect() {
    // Stops accepting first, so no new session can register a peer while the ones below are
    // being torn down.
    beast::error_code ec;
    acceptor_.close(ec);
    if (ec) {
        ERROR_PRINT("Acceptor close error: %s", ec.message().c_str());
    }

    peer_registry_.Stop();
}

webrtc::scoped_refptr<RtcPeer> WhepService::CreatePeer(PeerConfig config) {
    if (!conductor_) {
        ERROR_PRINT("Conductor is not initialized.");
        return nullptr;
    }

    auto peer = conductor_->CreatePeerConnection(config);
    if (!peer) {
        return nullptr;
    }
    peer_registry_.Add(peer);
    return peer;
}

webrtc::scoped_refptr<RtcPeer> WhepService::GetPeer(const std::string &peer_id) {
    return peer_registry_.Get(peer_id);
}

void WhepService::RemovePeer(const std::string &peer_id) { peer_registry_.Remove(peer_id); }

void WhepService::AcceptConnection() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            auto session = HttpSession::Create(std::move(socket), shared_from_this());
            session->Start();
        } else {
            std::cerr << "Accept error: " << ec.message() << "\n";
            if (!acceptor_.is_open()) {
                return; // Disconnect() closed the acceptor, so stop re-arming.
            }
        }
        AcceptConnection();
    });
}

std::shared_ptr<HttpSession> HttpSession::Create(tcp::socket socket,
                                                 std::shared_ptr<WhepService> whep_service) {
    return std::make_shared<HttpSession>(std::move(socket), whep_service);
}

HttpSession::HttpSession(tcp::socket socket, std::shared_ptr<WhepService> whep_service)
    : whep_service_(std::move(whep_service)),
      stream_(std::move(socket)),
      answer_timer_(stream_.get_executor()) {}

HttpSession::~HttpSession() {}

void HttpSession::ReadRequest() {
    auto self = shared_from_this();
    http::async_read(stream_, buffer_, req_,
                     [self](beast::error_code ec, std::size_t bytes_transferred) {
                         if (!ec) {
                             self->HandleRequest();
                         } else {
                             std::cerr << "Read error: " << ec.message() << "\n";
                         }
                     });
}

void HttpSession::WriteResponse() {
    auto self = shared_from_this();
    http::async_write(stream_, *res_, [self](beast::error_code ec, std::size_t bytes_transferred) {
        if (!ec) {
            DEBUG_PRINT("Successfully response!");
            self->CloseConnection();
        } else {
            std::cerr << "Write error: " << ec.message() << "\n";
        }
    });
}

void HttpSession::CloseConnection() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    if (ec) {
        std::cerr << "Shutdown error: " << ec.message() << "\n";
    }
}

void HttpSession::HandleRequest() {
    target_ = ParseWhepTarget(std::string(req_.target().data(), req_.target().size()));
    DEBUG_PRINT("Receive http method: %s %s",
                std::string(req_.method_string().data(), req_.method_string().size()).c_str(),
                std::string(req_.target().data(), req_.target().size()).c_str());

    switch (req_.method()) {
        case http::verb::post:
            HandlePostRequest();
            break;
        case http::verb::patch:
            HandlePatchRequest();
            break;
        case http::verb::options:
            HandleOptionsRequest();
            break;
        case http::verb::head:
            HandleHeadRequest();
            break;
        case http::verb::delete_:
            HandleDeleteRequest();
            break;
        default:
            RespondMethodNotAllowed();
            break;
    }
}

void HttpSession::HandlePostRequest() {
    if (target_.kind != WhepTarget::Kind::Endpoint) {
        target_.kind == WhepTarget::Kind::Session
            ? RespondMethodNotAllowed()
            : RespondError(http::status::not_found, "No WHEP endpoint at this path.");
        return;
    }

    if (MediaTypeOf(Header(http::field::content_type)) != kSdpType) {
        RespondError(http::status::unsupported_media_type,
                     "The offer must be sent with Content-Type `application/sdp`.");
        return;
    }

    PeerConfig config;
    config.has_candidates_in_sdp = true;
    auto peer = whep_service_->CreatePeer(config);
    if (!peer) {
        RespondError(http::status::internal_server_error, "Failed to create the peer connection.");
        return;
    }
    auto peer_id = peer->id();

    peer->OnLocalSdp([weak_self = weak_from_this()](const std::string &id, const std::string &sdp,
                                                    const std::string &type) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        boost::asio::post(self->stream_.get_executor(), [self, id, sdp]() {
            self->RespondCreated(id, sdp);
        });
    });

    answer_timer_.expires_after(std::chrono::seconds(kAnswerTimeoutSec));
    answer_timer_.async_wait([self = shared_from_this(), peer_id](beast::error_code ec) {
        if (ec || self->responded_) {
            return;
        }
        ERROR_PRINT("Peer (%s) produced no answer within %d seconds.", peer_id.c_str(),
                    kAnswerTimeoutSec);
        self->whep_service_->RemovePeer(peer_id);
        self->RespondError(http::status::service_unavailable,
                           "Timed out creating the SDP answer for this offer.");
    });

    peer->SetRemoteSdp(std::string(req_.body()), "offer");
}

void HttpSession::HandlePatchRequest() {
    auto peer = FindSessionPeer();
    if (!peer) {
        return;
    }

    auto content_type = MediaTypeOf(Header(http::field::content_type));
    if (content_type == kSdpType) {
        RespondError(http::status::unprocessable_entity,
                     "This session sent no counter-offer, so it expects no SDP answer.");
        return;
    }
    if (content_type != kTrickleIceType) {
        RespondError(http::status::unsupported_media_type,
                     "ICE updates must be sent with Content-Type "
                     "`application/trickle-ice-sdpfrag`.");
        return;
    }

    if (req_.find(http::field::if_match) == req_.end()) {
        RespondError(http::status::precondition_required,
                     "PATCH requires an If-Match header with the session's ETag, or `*` to "
                     "restart ICE.");
        return;
    }
    auto if_match = Header(http::field::if_match);
    auto ice_group = ParseCandidates(std::string(req_.body()));

    if (if_match == "*") {
        DEBUG_PRINT("peer (%s) ice restart!", target_.peer_id.c_str());
        auto local_sdp = peer->RestartIce(ice_group.ice_ufrag, ice_group.ice_pwd);
        if (local_sdp.empty()) {
            RespondError(http::status::unprocessable_entity, "The ICE restart failed.");
            return;
        }
        for (const auto &candidate : ice_group.candidates) {
            peer->SetRemoteIce("0", 0, candidate);
        }

        auto res = CreateResponse(http::status::ok);
        res->set(http::field::content_type, kTrickleIceType);
        auto etag = EntityTagOf(local_sdp);
        if (!etag.empty()) {
            res->set(http::field::etag, etag);
        }
        res->body() = local_sdp;
        Send(res);
        return;
    }

    if (if_match != EntityTagOf(LocalSdpOf(peer))) {
        RespondError(http::status::precondition_failed,
                     "If-Match does not name this session's current ICE session.");
        return;
    }

    for (const auto &candidate : ice_group.candidates) {
        DEBUG_PRINT("  Set remote ice: %s", candidate.c_str());
        peer->SetRemoteIce("0", 0, candidate);
    }
    DEBUG_PRINT("Set received candidates into peer (%s)!", target_.peer_id.c_str());

    Send(CreateResponse(http::status::no_content));
}

void HttpSession::HandleOptionsRequest() {
    auto res = CreateResponse(http::status::ok);
    res->set(http::field::access_control_allow_methods, "OPTIONS, HEAD, POST, PATCH, DELETE");
    res->set(http::field::access_control_allow_headers, "Content-Type, Authorization, If-Match");
    res->set(http::field::access_control_max_age, "86400");
    if (target_.kind == WhepTarget::Kind::Endpoint) {
        res->set("Accept-Post", kSdpType);
    }
    // Older browser builds preflight requests into a private network with this header.
    if (req_.find("Access-Control-Request-Private-Network") != req_.end()) {
        res->set("Access-Control-Allow-Private-Network", "true");
    }
    Send(res);
}

void HttpSession::HandleHeadRequest() {
    if (target_.kind != WhepTarget::Kind::Endpoint) {
        target_.kind == WhepTarget::Kind::Session
            ? RespondMethodNotAllowed()
            : RespondError(http::status::not_found, "No WHEP endpoint at this path.");
        return;
    }

    auto res = CreateResponse(http::status::ok);
    res->set(http::field::content_type, kSdpType);
    Send(res);
}

void HttpSession::HandleDeleteRequest() {
    auto peer = FindSessionPeer();
    if (!peer) {
        return;
    }

    whep_service_->RemovePeer(target_.peer_id); // terminates the peer
    DEBUG_PRINT("Close peer (%s)!", target_.peer_id.c_str());

    Send(CreateResponse(http::status::ok));
}

webrtc::scoped_refptr<RtcPeer> HttpSession::FindSessionPeer() {
    if (target_.kind != WhepTarget::Kind::Session) {
        target_.kind == WhepTarget::Kind::Endpoint
            ? RespondMethodNotAllowed()
            : RespondError(http::status::not_found, "No WHEP session at this path.");
        return nullptr;
    }

    auto peer = whep_service_->GetPeer(target_.peer_id);
    if (!peer) {
        RespondError(http::status::not_found, "The WHEP session does not exist.");
    }
    return peer;
}

std::string HttpSession::Header(http::field field) const {
    auto it = req_.find(field);
    if (it == req_.end()) {
        return "";
    }
    return std::string(it->value().data(), it->value().size());
}

std::shared_ptr<HttpSession::Response> HttpSession::CreateResponse(http::status status) {
    auto res = std::make_shared<Response>(status, req_.version());
    res->set(http::field::server, "pi-webrtc.whep");
    res->set(http::field::access_control_allow_origin, "*");
    // Without this, a cross-origin player cannot read the session URL it needs for DELETE.
    res->set(http::field::access_control_expose_headers, "Location, ETag, Link, Accept-Post");
    return res;
}

void HttpSession::Send(std::shared_ptr<Response> res) {
    responded_ = true;
    res->keep_alive(false);
    res->prepare_payload();
    res_ = std::move(res);
    WriteResponse();
}

void HttpSession::RespondCreated(const std::string &peer_id, const std::string &sdp) {
    if (responded_) {
        return;
    }
    answer_timer_.cancel();

    auto res = CreateResponse(http::status::created);
    res->set(http::field::content_type, kSdpType);
    res->set(http::field::location, std::string("/") + kSessionSegment + "/" + peer_id);
    auto etag = EntityTagOf(sdp);
    if (!etag.empty()) {
        res->set(http::field::etag, etag);
    }
    res->body() = sdp;
    Send(res);
}

void HttpSession::RespondError(http::status status, const char *message) {
    auto res = CreateResponse(status);
    res->set(http::field::content_type, "text/plain");
    res->body() = message;
    Send(res);
}

void HttpSession::RespondMethodNotAllowed() {
    auto res = CreateResponse(http::status::method_not_allowed);
    res->set(http::field::allow, target_.kind == WhepTarget::Kind::Session
                                     ? "OPTIONS, PATCH, DELETE"
                                     : "OPTIONS, HEAD, POST");
    res->set(http::field::content_type, "text/plain");
    res->body() = "This method is not allowed on this path.";
    Send(res);
}

IceCandidates HttpSession::ParseCandidates(const std::string &sdp) {
    std::regex iceUfragRegex(R"(a=ice-ufrag:([^\s]+))");
    std::regex icePwdRegex(R"(a=ice-pwd:([^\s]+))");
    std::regex candidateRegex(R"(a=candidate:(.*))");

    std::smatch match;
    auto sdpBegin = sdp.begin();
    auto sdpEnd = sdp.end();

    IceCandidates result;

    while (std::regex_search(sdpBegin, sdpEnd, match, candidateRegex)) {
        std::string candidate = match[1].str();
        result.candidates.push_back("candidate:" + candidate);
        sdpBegin = match.suffix().first;
    }

    if (std::regex_search(sdp, match, iceUfragRegex)) {
        result.ice_ufrag = match[1].str();
        DEBUG_PRINT("ice-ufrag: %s", result.ice_ufrag.c_str());
    }

    if (std::regex_search(sdp, match, icePwdRegex)) {
        result.ice_pwd = match[1].str();
        DEBUG_PRINT("ice-pwd: %s", result.ice_pwd.c_str());
    }

    return result;
}
