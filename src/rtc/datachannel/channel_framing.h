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
    virtual bool Decode(const webrtc::DataBuffer &buffer, std::string *out) = 0;
};

// Payload goes on the wire as-is.
class PlainFraming : public ChannelFraming {
  public:
    static std::unique_ptr<ChannelFraming> Create() { return std::make_unique<PlainFraming>(); }

    std::string Encode(const uint8_t *data, size_t size) override;
    bool Decode(const webrtc::DataBuffer &buffer, std::string *out) override;
};

// Payload rides inside a LiveKit `DataPacket.user` envelope.
class LiveKitFraming : public ChannelFraming {
  public:
    static std::unique_ptr<ChannelFraming> Create(std::string topic = "ipc_topic") {
        return std::make_unique<LiveKitFraming>(std::move(topic));
    }

    explicit LiveKitFraming(std::string topic = "ipc_topic");

    std::string Encode(const uint8_t *data, size_t size) override;
    bool Decode(const webrtc::DataBuffer &buffer, std::string *out) override;

  private:
    std::string topic_;
};

#endif // CHANNEL_FRAMING_H_
