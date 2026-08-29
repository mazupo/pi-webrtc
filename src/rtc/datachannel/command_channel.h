#ifndef COMMAND_CHANNEL_H_
#define COMMAND_CHANNEL_H_

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "proto/packet.pb.h"

#include "rtc/datachannel/rtc_channel.h"
#include "rtc/datachannel/stream_channel.h"

// The `command` channel inbound requests to handlers and send small responses back.
// Anything bulk is answered on the stream channel instead -- that split is what keeps
// a large response from head-of-line blocking the commands behind it.
class CommandChannel : public RtcChannel {
  public:
    struct Context {
        const protocol::Packet &packet;
        const protocol::Request &request;
        std::string request_id;
        CommandChannel *command;
        StreamChannel *stream;
    };

    using RequestCase = protocol::Request::PayloadCase;
    using Handler = std::function<void(const Context &)>;

    static std::shared_ptr<CommandChannel>
    Create(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
           std::unique_ptr<ChannelFraming> framing);

    CommandChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                   std::unique_ptr<ChannelFraming> framing);

    // The channel bulk answers go out on. Weak: both live in the peer's map, and neither
    // should keep the other alive.
    void SetStreamChannel(std::weak_ptr<StreamChannel> stream);

    void RegisterHandler(RequestCase request_case, Handler func);

    void Send(const std::string &request_id, const protocol::Response &response);

  protected:
    void OnPacket(const protocol::Packet &packet) override;

  private:
    std::weak_ptr<StreamChannel> stream_;

    std::mutex mutex_;
    std::map<RequestCase, std::vector<Handler>> handlers_;
};

#endif // COMMAND_CHANNEL_H_
