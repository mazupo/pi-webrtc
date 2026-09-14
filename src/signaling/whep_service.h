#ifndef WHEP_SERVICE_H_
#define WHEP_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "args.h"
#include "rtc/conductor.h"
#include "signaling/peer_registry.h"
#include "signaling/signaling_service.h"

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

struct IceCandidates {
    std::string ice_ufrag;
    std::string ice_pwd;
    std::vector<std::string> candidates;
};

// WHEP endpoint (`/` or `/<stream>`) or session (`/sessions/<peer_id>`).
struct WhepTarget {
    enum class Kind {
        Invalid,
        Endpoint,
        Session
    };

    Kind kind = Kind::Invalid;
    std::string stream;  // Endpoint; empty for `/`.
    std::string peer_id; // Session.
};

WhepTarget ParseWhepTarget(const std::string &target);

class WhepService : public SignalingService,
                    public std::enable_shared_from_this<WhepService> {
  public:
    static std::shared_ptr<WhepService> Create(Args args, std::shared_ptr<Conductor> conductor,
                                               boost::asio::io_context &ioc);

    WhepService(Args args, std::shared_ptr<Conductor> conductor, boost::asio::io_context &ioc);
    ~WhepService() override;

    void Connect() override;
    void Disconnect() override;

    webrtc::scoped_refptr<RtcPeer> CreatePeer(PeerConfig config = PeerConfig{});
    webrtc::scoped_refptr<RtcPeer> GetPeer(const std::string &peer_id);
    void RemovePeer(const std::string &peer_id);
    std::optional<std::string> ResolveStream(const std::string &stream) const;

  private:
    std::shared_ptr<Conductor> conductor_;
    uint16_t port_;
    tcp::acceptor acceptor_;
    PeerRegistry peer_registry_;

    void AcceptConnection();
};

class HttpSession : public std::enable_shared_from_this<HttpSession> {
  public:
    using Response = http::response<http::string_body>;

    static std::shared_ptr<HttpSession> Create(tcp::socket socket,
                                               std::shared_ptr<WhepService> whep_service);

    HttpSession(tcp::socket socket, std::shared_ptr<WhepService> whep_service);
    ~HttpSession();

    void Start() { ReadRequest(); }

  private:
    std::shared_ptr<WhepService> whep_service_;

    beast::tcp_stream stream_;
    boost::asio::steady_timer answer_timer_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<Response> res_;
    WhepTarget target_;

    bool responded_ = false;

    void ReadRequest();
    void WriteResponse();
    void CloseConnection();

    void HandleRequest();
    void HandlePostRequest();
    void HandlePatchRequest();
    void HandleOptionsRequest();
    void HandleHeadRequest();
    void HandleDeleteRequest();

    webrtc::scoped_refptr<RtcPeer> FindSessionPeer();
    std::string Header(http::field field) const;

    std::shared_ptr<Response> CreateResponse(http::status status);
    void Send(std::shared_ptr<Response> res);
    void RespondCreated(const std::string &peer_id, const std::string &sdp);
    void RespondError(http::status status, const char *message);
    void RespondMethodNotAllowed();

    IceCandidates ParseCandidates(const std::string &sdp);
};

#endif
