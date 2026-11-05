#ifndef STREAM_CHANNEL_H_
#define STREAM_CHANNEL_H_

#include <fstream>
#include <memory>
#include <string>

#include "codecs/jpeg/libjpeg_encoder.h"
#include "proto/packet.pb.h"

#include "rtc/datachannel/rtc_channel.h"

// This is the only class with file-transfer methods on it.
class StreamChannel : public RtcChannel {
  public:
    static std::shared_ptr<StreamChannel>
    Create(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
           std::unique_ptr<ChannelFraming> framing);

    StreamChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                  std::unique_ptr<ChannelFraming> framing);

    void Send(const std::string &request_id, const protocol::QueryFileResponse &response);
    void Send(const std::string &request_id, libjpeg_encoder::JpegBuffer image);
    void Send(const std::string &request_id, std::ifstream &file);

  protected:
    void OnPacket(const protocol::Packet &packet, const std::string &remote_id) override;
};

#endif // STREAM_CHANNEL_H_
