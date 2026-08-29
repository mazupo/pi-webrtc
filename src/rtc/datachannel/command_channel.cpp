#include "rtc/datachannel/command_channel.h"

#include "common/logging.h"

std::shared_ptr<CommandChannel>
CommandChannel::Create(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                       std::unique_ptr<ChannelFraming> framing) {
    return std::make_shared<CommandChannel>(std::move(data_channel), std::move(framing));
}

CommandChannel::CommandChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                               std::unique_ptr<ChannelFraming> framing)
    : RtcChannel(ChannelRole::Command, std::move(data_channel), std::move(framing)) {}

void CommandChannel::SetStreamChannel(std::weak_ptr<StreamChannel> stream) {
    stream_ = std::move(stream);
}

void CommandChannel::RegisterHandler(RequestCase request_case, Handler func) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[request_case].push_back(std::move(func));
}

void CommandChannel::OnPacket(const protocol::Packet &packet) {
    // This channel carries client requests and nothing else. A response or stream body
    // is the device's own direction of travel, and a raw body belongs to the IPC
    // channels -- neither has any business arriving here.
    if (!packet.has_request()) {
        DEBUG_PRINT("Ignoring packet with body case %d on the command channel",
                    static_cast<int>(packet.body_case()));
        return;
    }

    std::vector<Handler> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(packet.request().payload_case());
        if (it == handlers_.end()) {
            DEBUG_PRINT("No handler for request payload case %d",
                        static_cast<int>(packet.request().payload_case()));
            return;
        }
        handlers = it->second;
    }

    auto stream = stream_.lock();
    Context ctx{packet, packet.request(), packet.request_id(), this, stream.get()};

    for (auto &handler : handlers) {
        handler(ctx);
    }
}

void CommandChannel::Send(const std::string &request_id, const protocol::Response &response) {
    protocol::Packet pkt;
    pkt.set_request_id(request_id);
    *pkt.mutable_response() = response;
    RtcChannel::Send(pkt);
}
