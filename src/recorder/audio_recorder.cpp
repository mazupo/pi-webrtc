#include "recorder/audio_recorder.h"
#include "common/logging.h"

#include <thread>

std::unique_ptr<AudioRecorder> AudioRecorder::Create(int sample_rate) {
    auto ptr = std::make_unique<AudioRecorder>(sample_rate);
    ptr->InitializeFifoBuffer();
    return ptr;
}

AudioRecorder::AudioRecorder(int sample_rate)
    : Recorder(),
      sample_rate(sample_rate),
      channels(2),
      sample_fmt(AV_SAMPLE_FMT_FLTP),
      encoder_name("aac") {}

AudioRecorder::~AudioRecorder() {}

void AudioRecorder::InitializeEncoderCtx(AVCodecContext *&encoder) {
    const AVCodec *codec = avcodec_find_encoder_by_name(encoder_name.c_str());
    encoder = avcodec_alloc_context3(codec);
    encoder->codec_type = AVMEDIA_TYPE_AUDIO;
    encoder->sample_fmt = sample_fmt;
    encoder->bit_rate = 128000;
    encoder->sample_rate = sample_rate;
#if LIBAVCODEC_VERSION_MAJOR >= 60
    AVChannelLayout layout;
    av_channel_layout_default(&layout, channels);
    av_channel_layout_copy(&encoder->ch_layout, &layout);
#else
    encoder->channel_layout = av_get_default_channel_layout(channels);
    encoder->channels = channels;
#endif
    encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(encoder, codec, nullptr) < 0) {
        ERROR_PRINT("Failed to open audio codec: %s", encoder_name.c_str());
        return;
    }

    InitializeFrame();
}

void AudioRecorder::InitializeFrame() {
    frame = av_frame_alloc();
    frame_size = encoder->frame_size;
    if (frame != nullptr) {
        frame->pts = 0;
        frame->nb_samples = frame_size;
        frame->format = encoder->sample_fmt;
#if LIBAVCODEC_VERSION_MAJOR >= 60
        av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
#else
        frame->channel_layout = encoder->channel_layout;
        frame->channels = encoder->channels;
#endif
        frame->sample_rate = encoder->sample_rate;
    }
    av_frame_get_buffer(frame, 0);
    av_frame_make_writable(frame);
}

void AudioRecorder::InitializeFifoBuffer() { fifo_buffer.alloc(sample_fmt, channels, 1); }

void AudioRecorder::Encode() {
    if (fifo_buffer.read((void **)&frame->data, frame_size) < 0) {
        DEBUG_PRINT("Failed to read audio data in fifo.");
        return;
    }

    frame->pts = frame_count * frame->nb_samples;
    frame_count++;

    int ret = avcodec_send_frame(encoder, frame);
    if (ret < 0 || ret == AVERROR_EOF) {
        DEBUG_PRINT("Could not send packet to encoder");
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_packet(encoder, pkt);
        if (ret == AVERROR(EAGAIN)) {
            break;
        } else if (ret < 0) {
            DEBUG_PRINT("Failed to encode the frame");
            break;
        }

        pkt->stream_index = st->index;
        pkt->pts = av_rescale_q(pkt->pts, encoder->time_base, st->time_base);
        pkt->dts = av_rescale_q(pkt->dts, encoder->time_base, st->time_base);
        pkt->duration = av_rescale_q(pkt->duration, encoder->time_base, st->time_base);

        OnPacketed(pkt);
    }
    av_packet_unref(pkt);
    av_packet_free(&pkt);
}

void AudioRecorder::OnBuffer(AudioBuffer buffer) {
    uint8_t **converted_input_samples = nullptr;
    int samples_per_channel = buffer.length / buffer.channels;

    if (av_samples_alloc_array_and_samples(&converted_input_samples, nullptr, channels,
                                           samples_per_channel, sample_fmt, 0) < 0) {
        // Handle allocation error
        return;
    }

    auto data = reinterpret_cast<float *>(buffer.start);
    for (int i = 0; i < buffer.length; ++i) {
        int channel_index = i % buffer.channels;
        if (channel_index < channels) {
            reinterpret_cast<float *>(converted_input_samples[channel_index])[i / buffer.channels] =
                data[i];
        }
    }

    if (fifo_buffer.write(reinterpret_cast<void **>(converted_input_samples), samples_per_channel) <
        samples_per_channel) {
        DEBUG_PRINT("Failed to write audio data into fifo buffer.");
    }

    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        av_freep(&converted_input_samples);
        converted_input_samples = nullptr;
    }
}

bool AudioRecorder::ConsumeBuffer() {
    if (fifo_buffer.size() < frame_size) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return false;
    }
    Encode();
    return true;
}

void AudioRecorder::OnStart() {
    frame_count = 0;
    fifo_buffer.reset();
}
