#ifndef CHANNEL_FRAMING_H_
#define CHANNEL_FRAMING_H_

#include <cstdint>
#include <memory>
#include <string>

#include <api/data_channel_interface.h>

// How a payload is wrapped for the wire.
class ChannelFraming {
  public:
    virtual ~ChannelFraming() = default;

    // Returns the bytes to put on the wire, or an empty string to drop the message.
    virtual std::string Encode(const uint8_t *data, size_t size) = 0;
    // Returns false when the buffer holds nothing this channel should act on.
    // `remote_id` is who sent it: the peer, or the LiveKit participant.
    virtual bool Decode(const webrtc::DataBuffer &buffer, std::string *out,
                        std::string *remote_id) = 0;
};

// Payload goes on the wire as-is. Everything received comes from the one peer, `remote_id`.
class PlainFraming : public ChannelFraming {
  public:
    static std::unique_ptr<ChannelFraming> Create(std::string remote_id) {
        return std::make_unique<PlainFraming>(std::move(remote_id));
    }

    explicit PlainFraming(std::string remote_id);

    std::string Encode(const uint8_t *data, size_t size) override;
    bool Decode(const webrtc::DataBuffer &buffer, std::string *out,
                std::string *remote_id) override;

  private:
    std::string remote_id_;
};

// Payload rides inside a LiveKit `DataPacket.user` envelope.
class LiveKitFraming : public ChannelFraming {
  public:
    static std::unique_ptr<ChannelFraming> Create(std::string topic = "ipc_topic") {
        return std::make_unique<LiveKitFraming>(std::move(topic));
    }

    explicit LiveKitFraming(std::string topic = "ipc_topic");

    std::string Encode(const uint8_t *data, size_t size) override;
    bool Decode(const webrtc::DataBuffer &buffer, std::string *out,
                std::string *remote_id) override;

  private:
    std::string topic_;
};

#endif // CHANNEL_FRAMING_H_
