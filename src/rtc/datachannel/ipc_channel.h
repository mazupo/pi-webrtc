#ifndef IPC_CHANNEL_H_
#define IPC_CHANNEL_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "proto/packet.pb.h"

#include "ipc/unix_socket_server.h"
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
           std::unique_ptr<ChannelFraming> framing, std::shared_ptr<UnixSocketServer> ipc_server);

    IpcChannel(ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
               std::unique_ptr<ChannelFraming> framing,
               std::shared_ptr<UnixSocketServer> ipc_server);
    ~IpcChannel() override;

  protected:
    void OnPacket(const protocol::Packet &packet) override;

  private:
    // Only one channel may take socket traffic, or every local write reaches the browser
    // twice. Reliable gets it, so a device-sent message is not silently dropped.
    bool IsOutboundSink() const { return role() == ChannelRole::Reliable; }

    void OnStreamHeader(const std::string &stream_id, const protocol::Stream_Header &header);
    void OnStreamChunk(const std::string &stream_id, const protocol::Stream_Chunk &chunk);
    void OnStreamTrailer(const std::string &stream_id, const protocol::Stream_Trailer &trailer);
    void SendToPeer(const std::string &message);

    // A payload too large for one message, being reassembled. Chunking only happens on
    // the ordered channel, so a header always precedes its chunks.
    struct Assembly {
        std::string buffer;
        size_t received = 0;
    };

    std::shared_ptr<UnixSocketServer> ipc_server_;

    std::mutex mutex_;
    std::map<std::string, Assembly> assemblies_;
};

#endif // IPC_CHANNEL_H_
