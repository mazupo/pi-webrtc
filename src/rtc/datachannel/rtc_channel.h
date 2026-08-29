#ifndef RTC_CHANNEL_H_
#define RTC_CHANNEL_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "proto/packet.pb.h"
#include <api/data_channel_interface.h>

#include "rtc/datachannel/channel_framing.h"

// What a data channel is *for*. Label, stream id and SCTP options belong to the role;
// how a channel is opened and framed does not -- that varies by backend, applied by
// RtcPeer.
enum class ChannelRole {
    // Client requests and small device responses. Nothing large travels here, so nothing
    // can head-of-line block it.
    Command,
    // Bulk content, chunked: snapshots, query responses, file transfers. Unordered so
    // concurrent streams interleave rather than queue -- Stream.Chunk.offset carries the
    // placement. Still reliable: nothing here can ask for a chunk again.
    Stream,
    // IPC, udp-like. Unordered and un-retransmitted: late is as good as lost.
    Lossy,
    // IPC, tcp-like.
    Reliable,
};

// `_lossy` and `_reliable` are LiveKit's reserved labels, hence the underscores.
constexpr std::string_view RoleLabel(ChannelRole role) {
    switch (role) {
        case ChannelRole::Command:
            return "command";
        case ChannelRole::Stream:
            return "stream";
        case ChannelRole::Lossy:
            return "_lossy";
        case ChannelRole::Reliable:
            return "_reliable";
    }
    return "unknown";
}

constexpr std::optional<ChannelRole> RoleFromLabel(std::string_view label) {
    for (auto role :
         {ChannelRole::Command, ChannelRole::Stream, ChannelRole::Lossy, ChannelRole::Reliable}) {
        if (RoleLabel(role) == label) {
            return role;
        }
    }
    return std::nullopt;
}

// Default SCTP stream id of a role, used when the channel is negotiated out-of-band and
// nothing supplies one.
constexpr int RoleId(ChannelRole role) {
    switch (role) {
        case ChannelRole::Command:
            return 0;
        case ChannelRole::Stream:
            return 1;
        case ChannelRole::Lossy:
            return 2;
        case ChannelRole::Reliable:
            return 3;
    }
    return -1;
}

// The SCTP options a role needs. Whether it is also pre-negotiated depends on the
// backend, and RtcPeer applies that.
webrtc::DataChannelInit RoleInit(ChannelRole role);

// Bulk content is split at this size before being wrapped in a Stream.Chunk.
inline constexpr size_t kStreamChunkSize = 64 * 1024;

// Machinery shared by every data channel: an outbound queue so the caller is never
// blocked on a WebRTC thread, backpressure, framing, and the chunked-stream writer. What
// an inbound packet means is left to the subclass.
class RtcChannel : public webrtc::DataChannelObserver {
  public:
    RtcChannel(ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
               std::unique_ptr<ChannelFraming> framing);
    virtual ~RtcChannel();

    RtcChannel(const RtcChannel &) = delete;
    RtcChannel &operator=(const RtcChannel &) = delete;

    ChannelRole role() const { return role_; }
    std::string id() const { return id_; }
    std::string label() const { return label_; }
    bool IsOpen() const;

    void OnClosed(std::function<void()> func);
    void Terminate();

    // webrtc::DataChannelObserver
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer &buffer) override;

  protected:
    // What a packet arriving on this channel means.
    virtual void OnPacket(const protocol::Packet &packet) = 0;

    void Send(const protocol::Packet &packet);

    // Splits a body across a Stream header/chunks/trailer sharing one stream id. Kept
    // protected: plumbing, not a file-transfer method every channel is handed.
    void SendStream(const std::string &request_id, const std::string &mime_type,
                    const uint8_t *data, size_t size);
    void SendStream(const std::string &request_id, const std::string &mime_type,
                    std::ifstream &file);

    webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;

  private:
    void SendBytes(const uint8_t *data, size_t size);
    std::string SendStreamHeader(const std::string &request_id, const std::string &mime_type,
                                 size_t total_length);
    void SendStreamChunk(const std::string &stream_id, size_t offset, const char *data,
                         size_t size);
    void SendStreamTrailer(const std::string &stream_id, const std::string &reason);
    void SendLoop();
    void StopSendThread();

    ChannelRole role_;
    std::string id_;
    std::string label_;
    std::unique_ptr<ChannelFraming> framing_;

    std::mutex closed_mutex_;
    std::vector<std::function<void()>> on_closed_funcs_;

    std::deque<std::string> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    std::thread send_thread_;
    std::atomic<bool> send_thread_running_{false};
};

#endif // RTC_CHANNEL_H_
