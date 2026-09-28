#ifndef IPC_CHANNEL_H_
#define IPC_CHANNEL_H_

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "proto/packet.pb.h"

#include "ipc/endpoint_registry.h"
#include "rtc/datachannel/rtc_channel.h"

// Relays payloads between the local unix socket and the peer.
//
// Both the lossy and the reliable channel are always created and both are bidirectional,
// so the browser picks a delivery mode per message. The role decides the SCTP options,
// whether an oversized payload may be chunked (not on lossy), and which one is the
// outbound sink.
class IpcChannel : public RtcChannel {
  public:
    static std::shared_ptr<IpcChannel>
    Create(ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
           std::unique_ptr<ChannelFraming> framing, std::shared_ptr<EndpointRegistry> endpoints);

    IpcChannel(ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
               std::unique_ptr<ChannelFraming> framing,
               std::shared_ptr<EndpointRegistry> endpoints);
    ~IpcChannel() override;

  protected:
    void OnPacket(const protocol::Packet &packet, const std::string &remote_id) override;

  private:
    // Only one channel may take socket traffic, or every local write reaches the browser
    // twice. Reliable gets it, so a device-sent message is not silently dropped.
    bool IsOutboundSink() const { return role() == ChannelRole::Reliable; }

    void OnStreamHeader(const std::string &stream_id, const protocol::Stream_Header &header);
    void OnStreamChunk(const std::string &stream_id, const protocol::Stream_Chunk &chunk);
    void OnStreamTrailer(const std::string &stream_id, const protocol::Stream_Trailer &trailer,
                         const std::string &remote_id);
    void SendToPeer(const std::string &message);

    void WriteToEndpoint(const std::string &endpoint, const std::string &remote_id,
                         const std::string &payload);
    bool AcceptSequence(const std::string &remote_id, const std::string &endpoint,
                        uint64_t sequence);
    void NotifyRemotesClosed();

    // A payload too large for one message, being reassembled. Chunking only happens on
    // the ordered channel, so a header always precedes its chunks.
    struct Assembly {
        std::string buffer;
        size_t received = 0;
    };

    struct LastSequence {
        uint64_t sequence = 0;
        std::chrono::steady_clock::time_point received_at;
    };

    std::shared_ptr<EndpointRegistry> endpoints_;

    std::mutex mutex_;
    std::map<std::string, Assembly> assemblies_;
    std::map<std::pair<std::string, std::string>, LastSequence> last_sequence_;
    std::set<std::string> remote_ids_;
};

#endif // IPC_CHANNEL_H_
