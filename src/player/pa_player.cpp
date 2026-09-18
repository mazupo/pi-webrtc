#include "player/pa_player.h"

#include "common/logging.h"

// Server-side buffer target.
constexpr int kTargetLatencyMs = 40;

std::unique_ptr<AudioPlayer> PaPlayer::Create(int sample_rate, int channels) {
    return std::make_unique<PaPlayer>(sample_rate, channels);
}

PaPlayer::PaPlayer(int sample_rate, int channels)
    : AudioPlayer(sample_rate, channels),
      sink_(nullptr) {}

PaPlayer::~PaPlayer() { Close(); }

bool PaPlayer::Open() {
    if (sink_) {
        return true;
    }

    int error;
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.channels = channels_;
    ss.rate = sample_rate_;

    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.tlength = static_cast<uint32_t>(pa_usec_to_bytes(kTargetLatencyMs * 1000, &ss));
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);
    attr.fragsize = static_cast<uint32_t>(-1);

    sink_ = pa_simple_new(nullptr, "Speaker", PA_STREAM_PLAYBACK, nullptr, "playback", &ss, nullptr,
                          &attr, &error);
    if (!sink_) {
        ERROR_PRINT("%s", pa_strerror(error));
        return false;
    }

    INFO_PRINT("PulseAudio playout format: S16LE, %d channels, %d Hz", channels_, sample_rate_);
    return true;
}

void PaPlayer::Close() {
    if (sink_) {
        pa_simple_free(sink_);
        sink_ = nullptr;
    }
}

bool PaPlayer::Write(const int16_t *data, size_t frames) {
    if (!sink_) {
        return false;
    }

    int error;
    if (pa_simple_write(sink_, data, frames * channels_ * sizeof(int16_t), &error) < 0) {
        ERROR_PRINT("pa_simple_write() failed: %s", pa_strerror(error));
        Close();
        return false;
    }
    return true;
}

int PaPlayer::DelayMs() {
    if (!sink_) {
        return 0;
    }

    int error;
    pa_usec_t latency = pa_simple_get_latency(sink_, &error);
    if (latency == static_cast<pa_usec_t>(-1)) {
        return 0;
    }
    return static_cast<int>(latency / 1000);
}
