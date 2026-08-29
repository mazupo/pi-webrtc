#include "rtc/datachannel/channel_framing.h"

#include "proto/livekit_models.pb.h"

#include "common/logging.h"

std::string PlainFraming::Encode(const uint8_t *data, size_t size) {
    return std::string(reinterpret_cast<const char *>(data), size);
}

bool PlainFraming::Decode(const webrtc::DataBuffer &buffer, std::string *out) {
    out->assign(reinterpret_cast<const char *>(buffer.data.data<uint8_t>()), buffer.data.size());
    return true;
}

LiveKitFraming::LiveKitFraming(std::string topic)
    : topic_(std::move(topic)) {}

std::string LiveKitFraming::Encode(const uint8_t *data, size_t size) {
    livekit::DataPacket packet;
    auto *user = packet.mutable_user();
    user->set_payload(data, size);
    user->set_topic(topic_);

    std::string serialized;
    if (!packet.SerializeToString(&serialized)) {
        ERROR_PRINT("Failed to serialize LiveKit DataPacket");
        return {};
    }
    return serialized;
}

bool LiveKitFraming::Decode(const webrtc::DataBuffer &buffer, std::string *out) {
    livekit::DataPacket packet;
    if (!packet.ParseFromArray(buffer.data.data(), buffer.data.size())) {
        DEBUG_PRINT("Failed to parse LiveKit DataPacket");
        return false;
    }

    if (!packet.has_user()) {
        DEBUG_PRINT("Unknown LiveKit DataPacket type");
        return false;
    }

    const auto &user = packet.user();
    DEBUG_PRINT("Received USER packet: participant_identity=%s, topic=%s",
                packet.participant_identity().c_str(), user.topic().c_str());

    *out = user.payload();
    return true;
}
