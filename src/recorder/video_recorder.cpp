#include "recorder/video_recorder.h"

#include <memory>

#include "common/latency_tracer.h"
#include "common/logging.h"

namespace {

constexpr size_t kFrameQueueDepth = 2;

} // namespace

VideoRecorder::VideoRecorder(int width, int height, int fps, AVCodecID encoder_id)
    : Recorder(),
      fps(fps),
      width(width),
      height(height),
      encoder_id(encoder_id),
      frame_buffer_queue(kFrameQueueDepth),
      base_time_initialized(false) {}

void VideoRecorder::InitializeEncoderCtx(AVCodecContext *&encoder) {
    AVRational frame_rate = {.num = (int)fps, .den = 1};

    const AVCodec *codec = avcodec_find_encoder(encoder_id);
    encoder = avcodec_alloc_context3(codec);
    encoder->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder->width = width;
    encoder->height = height;
    encoder->framerate = frame_rate;
    encoder->time_base = av_inv_q(frame_rate);
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

void VideoRecorder::OnBuffer(V4L2FrameBufferRef frame_buffer) {
    if (frame_buffer_queue.full()) {
        if (latency::Enabled()) {
            latency::Count(latency::Counter::kRecorderQueueFull);
        }
        return;
    }

    // A dma-only frame is passed on by reference: it lives in NVMM and every encoder on that path
    // reads it through the dma fd, so there is nothing to copy. A mmap-backed one has to be copied
    // out first, because the driver takes it back as soon as this callback returns.
    frame_buffer_queue.push(frame_buffer->IsDmaOnly() ? frame_buffer : frame_buffer->Clone());
}

void VideoRecorder::OnStart() {
    {
        std::lock_guard<std::mutex> lock(encoder_mtx_);
        base_time_initialized = false;
        ReleaseEncoder();
    }
    frame_buffer_queue.clear();
}

void VideoRecorder::OnEncoded(uint8_t *start, uint32_t length, timeval timestamp, uint32_t flags) {
    if (!st) {
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (av_new_packet(pkt, length) < 0) {
        av_packet_free(&pkt);
        return;
    }
    memcpy(pkt->data, start, length);

    pkt->stream_index = st->index;
    if (flags & V4L2_BUF_FLAG_KEYFRAME) {
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    if (!base_time_initialized) {
        base_time_ = timestamp;
        base_time_initialized = true;
    }

    int64_t elapsed_usec = (int64_t)(timestamp.tv_sec - base_time_.tv_sec) * 1000000LL +
                           (int64_t)(timestamp.tv_usec - base_time_.tv_usec);

    AVRational usec_base = {1, 1000000};

    pkt->pts = pkt->dts = av_rescale_q(elapsed_usec, usec_base, st->time_base);

    OnPacketed(pkt);

    av_packet_free(&pkt);
}

bool VideoRecorder::ConsumeBuffer() {
    auto item = frame_buffer_queue.pop(10);

    if (!item) {
        return false;
    }

    auto frame_buffer = item.value();

    std::lock_guard<std::mutex> lock(encoder_mtx_);

    if (!IsEncoderReady()) {
        return false;
    }

    Encode(frame_buffer);

    return true;
}

bool VideoRecorder::IsEncoderReady() { return encoder != nullptr; }
