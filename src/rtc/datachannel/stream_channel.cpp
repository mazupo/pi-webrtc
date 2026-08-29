#include "rtc/datachannel/stream_channel.h"

#include "common/logging.h"

std::shared_ptr<StreamChannel>
StreamChannel::Create(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                      std::unique_ptr<ChannelFraming> framing) {
    return std::make_shared<StreamChannel>(std::move(data_channel), std::move(framing));
}

StreamChannel::StreamChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                             std::unique_ptr<ChannelFraming> framing)
    : RtcChannel(ChannelRole::Stream, std::move(data_channel), std::move(framing)) {}

void StreamChannel::OnPacket(const protocol::Packet &packet) {
    // The stream channel is device-to-client only; the client asks on the command channel.
    DEBUG_PRINT("Ignoring unexpected inbound packet with body case %d on the stream channel",
                static_cast<int>(packet.body_case()));
}

void StreamChannel::Send(const std::string &request_id,
                         const protocol::QueryFileResponse &response) {
    std::string body;
    if (!response.SerializeToString(&body)) {
        ERROR_PRINT("Failed to serialize QueryFileResponse");
        return;
    }

    SendStream(request_id, "application/x-protobuf", reinterpret_cast<const uint8_t *>(body.data()),
               body.size());
}

void StreamChannel::Send(const std::string &request_id, jpeg_util::JpegBuffer image) {
    SendStream(request_id, "image/jpeg", image.start.get(), image.length);
    DEBUG_PRINT("Image sent: %lu bytes", image.length);
}

void StreamChannel::Send(const std::string &request_id, std::ifstream &file) {
    SendStream(request_id, "application/octet-stream", file);
}
