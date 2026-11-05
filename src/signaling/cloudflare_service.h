#ifndef CLOUDFLARE_SERVICE_H_
#define CLOUDFLARE_SERVICE_H_

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio.hpp>

#include "proto/cloudflare.pb.h"
#include "rtc/peer_factory.h"
#include "signaling/signaling_config.h"
#include "signaling/signaling_service.h"
#include "signaling/websocket_client.h"

// Publishes to Cloudflare Realtime through a cf-sfu-worker
// (https://github.com/mazupo/cf-sfu-worker). The worker sends `wake` when someone watches one of
// the device's rooms and `sleep` when nobody does; only then is there a PeerConnection.
class CloudflareService : public SignalingService,
                          public std::enable_shared_from_this<CloudflareService> {
  public:
    static std::shared_ptr<CloudflareService> Create(CloudflareConfig config,
                                                     std::shared_ptr<PeerFactory> peer_factory,
                                                     boost::asio::io_context &ioc);
    CloudflareService(CloudflareConfig config, std::shared_ptr<PeerFactory> peer_factory,
                      boost::asio::io_context &ioc);
    ~CloudflareService() override;

    void Connect() override;
    void Disconnect() override;

  private:
    std::shared_ptr<PeerFactory> peer_factory_;
    CloudflareConfig config_;
    boost::asio::io_context &ioc_;
    boost::asio::steady_timer reconnect_timer_, health_timer_, retry_timer_;
    std::shared_ptr<WebSocketClient> socket_;
    webrtc::scoped_refptr<RtcPeer> peer_;
    std::vector<webrtc::PeerConnectionInterface::IceServer> ice_servers_;

    std::string token_url_;
    // Sent with each token request. Another process with this --uid has another one.
    const std::string secret_;
    // From wake until sleep: the worker wants this device to publish.
    bool publishing_ = false;
    // The generation of the answer the PeerConnection uses, or 0 before the answer.
    uint32_t generation_ = 0;
    // Offers sent on this connection with no reply yet. Only the reply to the last offer counts.
    int unanswered_ = 0;
    bool offer_sent_ = false;
    int retry_count_ = 0;
    bool stopped_ = false;
    bool reconnect_pending_ = false;
    bool ready_sent_ = false;
    bool peer_was_connected_ = false;
    std::chrono::steady_clock::time_point peer_down_since_;
    // Callbacks of an older connection or PeerConnection compare these and return.
    uint64_t connection_ = 0;
    uint64_t publication_ = 0;

    // connection to the worker
    void StartConnection();
    void RequestToken(uint64_t connection);
    void OpenSocket(uint64_t connection, const std::string &url, const std::string &token);
    void OnSocketClosed(int code, const std::string &reason);
    void ScheduleReconnect(const std::string &reason);
    void DropSocket(int code, const std::string &reason);
    bool Send(const protocol::DeviceRequest &request);
    void SendReady();

    // worker messages
    void OnMessage(const std::string &data);
    void OnJoined(const protocol::DeviceJoined &joined);
    void OnWake();
    void OnAnswer(const protocol::PublishAnswer &answer);
    void OnSleep();
    void OnError(const protocol::SignalError &error);
    void OnChannels(const protocol::DeviceChannels &channels);
    void OnControl(const protocol::ControlChanged &control);
    void OnVoicesOffer(const protocol::SubscribeOffer &offer);

    // publishing
    void StartPublishing();
    void StopPublishing();
    void RetryPublishing(const std::string &reason, int delay_sec);
    void OnPeerState(webrtc::PeerConnectionInterface::PeerConnectionState state);
    void MonitorPeer(uint64_t publication);
};

#endif // CLOUDFLARE_SERVICE_H_
