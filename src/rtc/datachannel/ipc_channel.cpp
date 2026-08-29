#include "rtc/datachannel/ipc_channel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "common/logging.h"

namespace {

// IPC payloads are opaque bytes. Rendering them as printable text lets a test read what
// the peer sent straight off the log, with no socket client attached.
[[maybe_unused]] std::string Preview(const std::string &data) {
    constexpr size_t kMaxPreview = 256;
    std::string out;
    for (size_t i = 0; i < std::min(data.size(), kMaxPreview); ++i) {
        auto c = static_cast<unsigned char>(data[i]);
        if (std::isprint(c)) {
            out += static_cast<char>(c);
        } else {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    if (data.size() > kMaxPreview) {
        out += "...";
    }
    return out;
}

} // namespace

std::shared_ptr<IpcChannel> IpcChannel::Create(
    ChannelRole role, webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
    std::unique_ptr<ChannelFraming> framing, std::shared_ptr<UnixSocketServer> ipc_server) {
    return std::make_shared<IpcChannel>(role, std::move(data_channel), std::move(framing),
                                        std::move(ipc_server));
}

IpcChannel::IpcChannel(ChannelRole role,
                       webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                       std::unique_ptr<ChannelFraming> framing,
                       std::shared_ptr<UnixSocketServer> ipc_server)
    : RtcChannel(role, std::move(data_channel), std::move(framing)),
      ipc_server_(std::move(ipc_server)) {
    if (ipc_server_ && IsOutboundSink()) {
        ipc_server_->RegisterPeerCallback(id(), [this](const std::string &msg) {
            SendToPeer(msg);
        });
    }
}

IpcChannel::~IpcChannel() {
    if (ipc_server_ && IsOutboundSink()) {
        ipc_server_->UnregisterPeerCallback(id());
    }
}

void IpcChannel::OnPacket(const protocol::Packet &packet) {
    // A raw body is by definition an IPC payload, and an oversized one arrives as a
    // stream on this same channel. Anything else does not belong here.
    if (packet.has_raw()) {
        DEBUG_PRINT("(%s) Received %zu bytes: %s", label().c_str(), packet.raw().size(),
                    Preview(packet.raw()).c_str());
        if (ipc_server_) {
            ipc_server_->Write(packet.raw());
        }
        return;
    }

    if (!packet.has_stream()) {
        DEBUG_PRINT("Ignoring packet with body case %d on an IPC channel",
                    static_cast<int>(packet.body_case()));
        return;
    }

    const auto &stream = packet.stream();
    switch (stream.payload_case()) {
        case protocol::Stream::kHeader:
            OnStreamHeader(stream.stream_id(), stream.header());
            break;
        case protocol::Stream::kChunk:
            OnStreamChunk(stream.stream_id(), stream.chunk());
            break;
        case protocol::Stream::kTrailer:
            OnStreamTrailer(stream.stream_id(), stream.trailer());
            break;
        default:
            ERROR_PRINT("IPC stream packet without a payload");
            break;
    }
}

void IpcChannel::OnStreamHeader(const std::string &stream_id,
                                const protocol::Stream_Header &header) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &assembly = assemblies_[stream_id];
    assembly.buffer.assign(header.total_length(), '\0');
    assembly.received = 0;
    DEBUG_PRINT("(%s) Receiving stream %s of %zu bytes", label().c_str(), stream_id.c_str(),
                static_cast<size_t>(header.total_length()));
}

void IpcChannel::OnStreamChunk(const std::string &stream_id, const protocol::Stream_Chunk &chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assemblies_.find(stream_id);
    if (it == assemblies_.end()) {
        ERROR_PRINT("IPC chunk for unknown stream %s", stream_id.c_str());
        return;
    }

    const auto &data = chunk.data();
    if (chunk.offset() + data.size() > it->second.buffer.size()) {
        ERROR_PRINT("IPC chunk overruns the declared length of stream %s", stream_id.c_str());
        assemblies_.erase(it);
        return;
    }

    it->second.buffer.replace(chunk.offset(), data.size(), data);
    it->second.received += data.size();
}

void IpcChannel::OnStreamTrailer(const std::string &stream_id,
                                 const protocol::Stream_Trailer &trailer) {
    std::string body;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assemblies_.find(stream_id);
        if (it == assemblies_.end()) {
            return;
        }

        if (!trailer.reason().empty()) {
            DEBUG_PRINT("IPC stream %s aborted: %s", stream_id.c_str(), trailer.reason().c_str());
        } else if (it->second.received != it->second.buffer.size()) {
            ERROR_PRINT("IPC stream %s ended with %zu of %zu bytes", stream_id.c_str(),
                        it->second.received, it->second.buffer.size());
        } else {
            body = std::move(it->second.buffer);
        }
        assemblies_.erase(it);
    }

    if (body.empty()) {
        return;
    }

    DEBUG_PRINT("(%s) Received stream %s, %zu bytes: %s", label().c_str(), stream_id.c_str(),
                body.size(), Preview(body).c_str());
    if (ipc_server_) {
        ipc_server_->Write(body);
    }
}

void IpcChannel::SendToPeer(const std::string &message) {
    if (!IsOpen()) {
        return;
    }

    // Small payloads ride flat, which is what the browser sends us too. Only an
    // oversized one is chunked, and only here on the ordered channel.
    if (message.size() > kStreamChunkSize) {
        SendStream("", "application/octet-stream",
                   reinterpret_cast<const uint8_t *>(message.data()), message.size());
        return;
    }

    protocol::Packet pkt;
    pkt.set_raw(message);
    Send(pkt);
}
