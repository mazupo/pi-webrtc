#ifndef MQTT_SERVICE_H_
#define MQTT_SERVICE_H_

#include "signaling/signaling_config.h"
#include "signaling/signaling_service.h"

#include <memory>
#include <mosquitto.h>
#include <mutex>
#include <string>
#include <unordered_map>

#include "rtc/conductor.h"
#include "signaling/peer_registry.h"

class MqttService : public SignalingService {
  public:
    static std::shared_ptr<MqttService> Create(MqttConfig config,
                                               std::shared_ptr<Conductor> conductor);

    MqttService(MqttConfig config, std::shared_ptr<Conductor> conductor);
    ~MqttService() override;

    enum class TopicType {
        Offer,
        Answer,
        Ice,
        Unknown
    };

    void Connect() override;
    void Disconnect() override;

  private:
    std::shared_ptr<Conductor> conductor_;
    int port_;
    std::string uid_;
    std::string hostname_;
    std::string username_;
    std::string password_;
    struct mosquitto *connection_;

    std::mutex client_mutex_;
    std::unordered_map<std::string, std::string> client_id_to_peer_id_;
    std::unordered_map<std::string, std::string> peer_id_to_client_id_;

    PeerRegistry peer_registry_;

    webrtc::scoped_refptr<RtcPeer> CreatePeer(PeerConfig config);
    void OnPeerExpired(const std::string &peer_id);

    void OnRemoteSdp(const std::string &peer_id, const std::string &message);
    void OnRemoteIce(const std::string &peer_id, const std::string &message);
    void OnLocalSdp(const std::string &peer_id, const std::string &sdp, const std::string &type);
    void OnLocalIce(const std::string &peer_id, const std::string &sdp_mid,
                    const int sdp_mline_index, const std::string &candidate);

    void Subscribe(const std::string &topic);
    void Unsubscribe(const std::string &topic);
    void Publish(const std::string &topic, const std::string &msg);
    void OnConnect(struct mosquitto *mosq, void *obj, int rc);
    void OnMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);
    std::string FindClientId(const std::string &topic) const;
    std::string GetTopic(TopicType type, const std::string &client_id = "") const;
    std::string TopicTypeToString(TopicType type) const;
    TopicType FindTopicType(const std::string &topic) const;
};

#endif
