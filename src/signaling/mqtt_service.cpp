#include "signaling/mqtt_service.h"

#include <mqtt_protocol.h>
#include <nlohmann/json.hpp>

#include "common/logging.h"

std::shared_ptr<MqttService> MqttService::Create(MqttConfig config,
                                                 std::shared_ptr<PeerFactory> peer_factory) {
    return std::make_shared<MqttService>(config, peer_factory);
}

MqttService::MqttService(MqttConfig config, std::shared_ptr<PeerFactory> peer_factory)
    : peer_factory_(peer_factory),
      port_(config.port),
      uid_(config.uid),
      hostname_(config.host),
      username_(config.username),
      password_(config.password),
      connection_(nullptr) {
    peer_registry_.OnExpired([this](const std::string &peer_id) {
        OnPeerExpired(peer_id);
    });
}

MqttService::~MqttService() { Disconnect(); }

webrtc::scoped_refptr<RtcPeer> MqttService::CreatePeer(PeerConfig config) {
    if (!peer_factory_) {
        ERROR_PRINT("The peer factory is not initialized.");
        return nullptr;
    }

    auto peer = peer_factory_->CreatePeerConnection(config);
    peer_registry_.Add(peer);
    return peer;
}

// Called from the cleaner thread once the expired peer is already out of the registry.
void MqttService::OnPeerExpired(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(client_mutex_);

    auto it_c = peer_id_to_client_id_.find(peer_id);
    if (it_c == peer_id_to_client_id_.end()) {
        return;
    }

    auto it_p = client_id_to_peer_id_.find(it_c->second);
    if (it_p != client_id_to_peer_id_.end() && it_p->second == peer_id) {
        client_id_to_peer_id_.erase(it_p);
    }

    peer_id_to_client_id_.erase(it_c);

    DEBUG_PRINT("Peer %s expired, removed from registry and client mapping.", peer_id.c_str());
}

void MqttService::OnRemoteSdp(const std::string &peer_id, const std::string &message) {
    nlohmann::json jsonObj = nlohmann::json::parse(message);
    std::string sdp = jsonObj["sdp"];
    std::string type = jsonObj["type"];
    DEBUG_PRINT("Received remote [%s] SDP: %s", type.c_str(), sdp.c_str());

    auto peer = peer_registry_.Get(peer_id);
    if (peer) {
        peer->SetRemoteSdp(sdp, type);
    }
}

void MqttService::OnRemoteIce(const std::string &peer_id, const std::string &message) {
    nlohmann::json jsonObj = nlohmann::json::parse(message);
    std::string sdp_mid = jsonObj["sdpMid"];
    int sdp_mline_index = jsonObj["sdpMLineIndex"];
    std::string candidate = jsonObj["candidate"];
    DEBUG_PRINT("Received remote ICE: %s, %d, %s", sdp_mid.c_str(), sdp_mline_index,
                candidate.c_str());

    auto peer = peer_registry_.Get(peer_id);
    if (peer) {
        peer->SetRemoteIce(sdp_mid, sdp_mline_index, candidate);
    }
}

void MqttService::OnLocalSdp(const std::string &peer_id, const std::string &sdp,
                             const std::string &type) {
    DEBUG_PRINT("Sent local [%s] SDP: %s", type.c_str(), sdp.c_str());
    nlohmann::json jsonData;
    jsonData["type"] = type;
    jsonData["sdp"] = sdp;
    std::string jsonString = jsonData.dump();

    std::string client_id;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        auto it = peer_id_to_client_id_.find(peer_id);
        if (it == peer_id_to_client_id_.end()) {
            ERROR_PRINT("No client mapping for peer %s, cannot send SDP", peer_id.c_str());
            return;
        }
        client_id = it->second;
    }

    if (type == "offer") {
        Publish(GetTopic(TopicType::Offer, client_id), jsonString);
    } else if (type == "answer") {
        Publish(GetTopic(TopicType::Answer, client_id), jsonString);
    }
}

void MqttService::OnLocalIce(const std::string &peer_id, const std::string &sdp_mid,
                             const int sdp_mline_index, const std::string &candidate) {
    DEBUG_PRINT("Sent local ICE:  %s, %d, %s", sdp_mid.c_str(), sdp_mline_index, candidate.c_str());
    nlohmann::json jsonData;
    jsonData["sdpMid"] = sdp_mid;
    jsonData["sdpMLineIndex"] = sdp_mline_index;
    jsonData["candidate"] = candidate;
    std::string jsonString = jsonData.dump();

    std::string client_id;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        auto it = peer_id_to_client_id_.find(peer_id);
        if (it == peer_id_to_client_id_.end()) {
            ERROR_PRINT("No client mapping for peer %s, cannot send ICE", peer_id.c_str());
            return;
        }
        client_id = it->second;
    }

    Publish(GetTopic(TopicType::Ice, client_id), jsonString);
}

void MqttService::Disconnect() {
    if (!connection_) {
        DEBUG_PRINT("MQTT service already released.");
        peer_registry_.Stop();
        mosquitto_lib_cleanup();
        return;
    }

    int rc = mosquitto_loop_stop(connection_, true);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR_PRINT("mosquitto_loop_stop: %s", mosquitto_strerror(rc));
    }

    rc = mosquitto_disconnect(connection_);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR_PRINT("mosquitto_disconnect: %s", mosquitto_strerror(rc));
    }

    mosquitto_destroy(connection_);
    connection_ = nullptr;

    peer_registry_.Stop();
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_id_to_peer_id_.clear();
        peer_id_to_client_id_.clear();
    }

    mosquitto_lib_cleanup();
    DEBUG_PRINT("MQTT service is released.");
};

void MqttService::Publish(const std::string &topic, const std::string &msg) {
    int rc = mosquitto_publish_v5(connection_, NULL, topic.c_str(), msg.length(), msg.c_str(), 2,
                                  false, nullptr);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR_PRINT("Error publishing: %s", mosquitto_strerror(rc));
    }
}

void MqttService::Subscribe(const std::string &topic) {
    int subscribe_result = mosquitto_subscribe_v5(connection_, nullptr, topic.c_str(), 0,
                                                  MQTT_SUB_OPT_NO_LOCAL, nullptr);
    if (subscribe_result == MOSQ_ERR_SUCCESS) {
        DEBUG_PRINT("Successfully subscribed to topic: %s", topic.c_str());
    } else {
        DEBUG_PRINT("Failed to subscribe topic: %s", topic.c_str());
    }
}

void MqttService::Unsubscribe(const std::string &topic) {
    int unsubscribe_result = mosquitto_unsubscribe_v5(connection_, nullptr, topic.c_str(), nullptr);
    if (unsubscribe_result == MOSQ_ERR_SUCCESS) {
        DEBUG_PRINT("Successfully unsubscribed to topic: %s", topic.c_str());
    } else {
        DEBUG_PRINT("Failed to unsubscribe topic: %s", topic.c_str());
    }
}

void MqttService::OnConnect(struct mosquitto *mosq, void *obj, int rc) {
    if (rc == 0) {
        INFO_PRINT("MQTT connected to broker %s:%d", hostname_.c_str(), port_);
        Subscribe(GetTopic(TopicType::Offer, "+"));
        Subscribe(GetTopic(TopicType::Answer, "+"));
        Subscribe(GetTopic(TopicType::Ice, "+"));
        INFO_PRINT("MQTT service is ready.");
    } else {
        ERROR_PRINT("MQTT connect failed: %s", mosquitto_strerror(rc));
    }
}

void MqttService::OnMessage(struct mosquitto *mosq, void *obj,
                            const struct mosquitto_message *message) {
    if (!message->payload)
        return;

    std::string topic(message->topic);
    std::string payload(static_cast<char *>(message->payload));
    TopicType topic_type = FindTopicType(topic);
    if (topic_type == TopicType::Unknown) {
        ERROR_PRINT("Unknown MQTT topic: %s", topic.c_str());
        return;
    }

    auto client_id = FindClientId(topic);
    if (client_id.empty()) {
        ERROR_PRINT("Missing client id in MQTT topic: %s", topic.c_str());
        return;
    }

    std::string mapped_peer_id;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        auto it = client_id_to_peer_id_.find(client_id);
        if (it != client_id_to_peer_id_.end()) {
            mapped_peer_id = it->second;
        }
    }

    webrtc::scoped_refptr<RtcPeer> peer;
    if (!mapped_peer_id.empty()) {
        peer = peer_registry_.Get(mapped_peer_id);
    }

    if (!peer) {
        PeerConfig config;
        config.data_channel_only = true; // tracks added below only if SDP has media
        peer = CreatePeer(config);
        if (!peer) {
            ERROR_PRINT("Failed to create peer for client: %s", client_id.c_str());
            return;
        }

        peer->OnLocalSdp(
            [this](const std::string &peer_id, const std::string &sdp, const std::string &type) {
                OnLocalSdp(peer_id, sdp, type);
            });
        peer->OnLocalIce([this](const std::string &peer_id, const std::string &sdp_mid,
                                int sdp_mline_index, const std::string &candidate) {
            OnLocalIce(peer_id, sdp_mid, sdp_mline_index, candidate);
        });

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_id_to_peer_id_[client_id] = peer->id();
            peer_id_to_client_id_[peer->id()] = client_id;
        }

        DEBUG_PRINT("Created peer %s for client: %s", peer->id().c_str(), client_id.c_str());
    }

    if (topic_type == TopicType::Offer) {
        bool has_media = (payload.find("m=video") != std::string::npos ||
                          payload.find("m=audio") != std::string::npos);
        if (has_media) {
            peer_factory_->EnsureTracksAdded(peer);
        }
        OnRemoteSdp(peer->id(), payload);
    } else if (topic_type == TopicType::Answer) {
        DEBUG_PRINT("Received renegotiation answer from client: %s", client_id.c_str());
        OnRemoteSdp(peer->id(), payload);
    } else if (topic_type == TopicType::Ice) {
        OnRemoteIce(peer->id(), payload);
    }
}

std::string MqttService::FindClientId(const std::string &topic) const {
    TopicType topic_type = FindTopicType(topic);
    if (topic_type == TopicType::Unknown) {
        return "";
    }

    std::string base_topic = GetTopic(topic_type);
    if (topic.length() <= base_topic.length() || topic[base_topic.length()] != '/') {
        return "";
    }

    size_t start_pos = base_topic.length() + 1; // skip the trailing "/" of base_topic
    size_t end_pos = topic.find('/', start_pos);

    if (start_pos >= topic.length()) {
        return "";
    }

    if (end_pos == std::string::npos) {
        return topic.substr(start_pos);
    }

    return topic.substr(start_pos, end_pos - start_pos);
}

void MqttService::Connect() {
    peer_registry_.Start();
    mosquitto_lib_init();

    connection_ = mosquitto_new(NULL, true, this);
    if (connection_ == nullptr) {
        ERROR_PRINT("Failed to new mosquitto object.");
        return;
    }

    mosquitto_int_option(connection_, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V5);

    if (port_ == 8883) {
        mosquitto_int_option(connection_, MOSQ_OPT_TLS_USE_OS_CERTS, 1);
    }

    if (!username_.empty()) {
        mosquitto_username_pw_set(connection_, username_.c_str(), password_.c_str());
    }

    /* Configure callbacks. This should be done before connecting ideally. */
    mosquitto_connect_callback_set(connection_, [](struct mosquitto *mosq, void *obj, int rc) {
        MqttService *service = static_cast<MqttService *>(obj);
        service->OnConnect(mosq, obj, rc);
    });
    mosquitto_disconnect_callback_set(connection_, [](struct mosquitto *mosq, void *obj, int rc) {
        if (rc != 0) {
            ERROR_PRINT("Unexpected MQTT disconnect: %s", mosquitto_strerror(rc));
        } else {
            INFO_PRINT("MQTT disconnected normally.");
        }
    });
    mosquitto_message_v5_callback_set(connection_, [](struct mosquitto *mosq, void *obj,
                                                      const struct mosquitto_message *message,
                                                      const mosquitto_property *) {
        MqttService *service = static_cast<MqttService *>(obj);
        service->OnMessage(mosq, obj, message);
    });

    mosquitto_reconnect_delay_set(connection_, 2, 10, true);

    int rc = mosquitto_loop_start(connection_);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR_PRINT("mosquitto_loop_start: %s", mosquitto_strerror(rc));
        Disconnect();
        return;
    }

    INFO_PRINT("Trying to connect to MQTT Broker %s:%d", hostname_.c_str(), port_);
    rc = mosquitto_connect_async(connection_, hostname_.c_str(), port_, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR_PRINT("mosquitto_connect_async: %s", mosquitto_strerror(rc));
    }
}

// MQTT topic format:
// `{uid}/{type}/{client_id}` for client-specific messages, `{uid}/{type}` for broadcast messages
std::string MqttService::GetTopic(TopicType type, const std::string &client_id) const {
    if (client_id.empty()) {
        return uid_ + "/" + TopicTypeToString(type);
    }

    return uid_ + "/" + TopicTypeToString(type) + "/" + client_id;
}

std::string MqttService::TopicTypeToString(TopicType type) const {
    switch (type) {
        case TopicType::Offer:
            return "offer";
        case TopicType::Answer:
            return "answer";
        case TopicType::Ice:
            return "ice";
        default:
            return "unknown";
    }
}

MqttService::TopicType MqttService::FindTopicType(const std::string &topic) const {
    if (topic.starts_with(GetTopic(TopicType::Offer) + "/")) {
        return TopicType::Offer;
    }
    if (topic.starts_with(GetTopic(TopicType::Answer) + "/")) {
        return TopicType::Answer;
    }
    if (topic.starts_with(GetTopic(TopicType::Ice) + "/")) {
        return TopicType::Ice;
    }

    return TopicType::Unknown;
}
