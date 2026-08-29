#include "rtc/datachannel/rtc_channel.h"

#include <algorithm>

#include "common/logging.h"
#include "common/utils.h"

webrtc::DataChannelInit RoleInit(ChannelRole role) {
    webrtc::DataChannelInit init;
    switch (role) {
        case ChannelRole::Command:
        case ChannelRole::Reliable:
            init.ordered = true;
            break;
        case ChannelRole::Stream:
            init.ordered = false;
            break;
        case ChannelRole::Lossy:
            init.ordered = false;
            init.maxRetransmits = 0;
            break;
    }

    return init;
}

RtcChannel::RtcChannel(ChannelRole role,
                       webrtc::scoped_refptr<webrtc::DataChannelInterface> data_channel,
                       std::unique_ptr<ChannelFraming> framing)
    : data_channel(data_channel),
      role_(role),
      id_(utils::GenerateUuid()),
      label_(data_channel->label()),
      framing_(std::move(framing)),
      send_thread_running_(true) {
    data_channel->RegisterObserver(this);
    send_thread_ = std::thread(&RtcChannel::SendLoop, this);
}

RtcChannel::~RtcChannel() {
    StopSendThread();
    DEBUG_PRINT("datachannel (%s) is released!", label_.c_str());
}

bool RtcChannel::IsOpen() const {
    return data_channel->state() == webrtc::DataChannelInterface::kOpen;
}

void RtcChannel::OnStateChange() {
    auto state = data_channel->state();
    DEBUG_PRINT("[%s] OnStateChange => %s", label_.c_str(),
                webrtc::DataChannelInterface::DataStateString(state));
}

void RtcChannel::OnClosed(std::function<void()> func) {
    std::lock_guard<std::mutex> lock(closed_mutex_);
    on_closed_funcs_.push_back(std::move(func));
}

void RtcChannel::StopSendThread() {
    send_thread_running_.store(false);
    send_cv_.notify_all();
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
}

void RtcChannel::Terminate() {
    StopSendThread();

    data_channel->UnregisterObserver();
    data_channel->Close();

    std::vector<std::function<void()>> funcs;
    {
        std::lock_guard<std::mutex> lock(closed_mutex_);
        funcs.swap(on_closed_funcs_);
    }
    for (auto &func : funcs) {
        func();
    }
}

void RtcChannel::OnMessage(const webrtc::DataBuffer &buffer) {
    std::string payload;
    if (!framing_->Decode(buffer, &payload)) {
        return;
    }

    protocol::Packet packet;
    if (!packet.ParseFromString(payload)) {
        ERROR_PRINT("(%s) Failed to parse incoming packet", label_.c_str());
        return;
    }

    DEBUG_PRINT("(%s) Received packet body case: %d", label_.c_str(),
                static_cast<int>(packet.body_case()));

    OnPacket(packet);
}

void RtcChannel::Send(const protocol::Packet &packet) {
    std::string buf;
    if (!packet.SerializeToString(&buf)) {
        ERROR_PRINT("(%s) Failed to serialize outgoing packet", label_.c_str());
        return;
    }
    SendBytes(reinterpret_cast<const uint8_t *>(buf.data()), buf.size());
}

void RtcChannel::SendBytes(const uint8_t *data, size_t size) {
    std::string framed = framing_->Encode(data, size);
    if (framed.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push_back(std::move(framed));
    }
    send_cv_.notify_one();
}

void RtcChannel::SendLoop() {
    while (true) {
        std::string buf;
        {
            std::unique_lock<std::mutex> lock(send_mutex_);
            send_cv_.wait(lock, [this] {
                return !send_queue_.empty() || !send_thread_running_.load();
            });
            if (!send_thread_running_.load() && send_queue_.empty()) {
                return;
            }
            buf = std::move(send_queue_.front());
            send_queue_.pop_front();
        }

        while (send_thread_running_.load() &&
               data_channel->state() == webrtc::DataChannelInterface::kOpen &&
               data_channel->buffered_amount() + buf.size() > data_channel->MaxSendQueueSize()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (!send_thread_running_.load() ||
            data_channel->state() != webrtc::DataChannelInterface::kOpen) {
            return;
        }

        webrtc::CopyOnWriteBuffer buffer(buf.data(), buf.size());
        webrtc::DataBuffer data_buffer(buffer, true);
        data_channel->Send(data_buffer);
    }
}

std::string RtcChannel::SendStreamHeader(const std::string &request_id,
                                         const std::string &mime_type, size_t total_length) {
    auto stream_id = utils::GenerateUuid();

    protocol::Packet pkt;
    pkt.set_request_id(request_id);
    auto *stream = pkt.mutable_stream();
    stream->set_stream_id(stream_id);
    auto *header = stream->mutable_header();
    header->set_total_length(total_length);
    if (!mime_type.empty()) {
        header->set_mime_type(mime_type);
    }
    Send(pkt);

    return stream_id;
}

void RtcChannel::SendStreamChunk(const std::string &stream_id, size_t offset, const char *data,
                                 size_t size) {
    protocol::Packet pkt;
    auto *stream = pkt.mutable_stream();
    stream->set_stream_id(stream_id);
    auto *chunk = stream->mutable_chunk();
    chunk->set_offset(offset);
    chunk->set_data(data, size);
    Send(pkt);
}

void RtcChannel::SendStreamTrailer(const std::string &stream_id, const std::string &reason) {
    protocol::Packet pkt;
    auto *stream = pkt.mutable_stream();
    stream->set_stream_id(stream_id);
    auto *trailer = stream->mutable_trailer();
    if (!reason.empty()) {
        trailer->set_reason(reason);
    }
    Send(pkt);
}

void RtcChannel::SendStream(const std::string &request_id, const std::string &mime_type,
                            const uint8_t *data, size_t size) {
    auto stream_id = SendStreamHeader(request_id, mime_type, size);

    size_t offset = 0;
    while (offset < size) {
        auto read_size = std::min(kStreamChunkSize, size - offset);
        SendStreamChunk(stream_id, offset, reinterpret_cast<const char *>(data) + offset,
                        read_size);
        offset += read_size;
    }

    SendStreamTrailer(stream_id, "");
}

void RtcChannel::SendStream(const std::string &request_id, const std::string &mime_type,
                            std::ifstream &file) {
    if (!file.is_open()) {
        ERROR_PRINT("SendStream: file is not open, aborting transfer");
        return;
    }

    file.seekg(0, std::ios::end);
    size_t total_size = file.tellg();
    file.seekg(0, std::ios::beg);

    auto stream_id = SendStreamHeader(request_id, mime_type, total_size);

    std::vector<char> buffer(kStreamChunkSize);
    size_t offset = 0;
    while (file.read(buffer.data(), kStreamChunkSize) || file.gcount() > 0) {
        size_t read_size = file.gcount();
        SendStreamChunk(stream_id, offset, buffer.data(), read_size);
        offset += read_size;
    }

    if (offset != total_size) {
        ERROR_PRINT("File transfer read %zu of %zu bytes, aborting stream", offset, total_size);
        SendStreamTrailer(stream_id, "read error");
        return;
    }

    SendStreamTrailer(stream_id, "");
}
