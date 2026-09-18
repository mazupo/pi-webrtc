#include "player/alsa_player.h"

#include "common/logging.h"

constexpr unsigned int kTargetLatencyUs = 60000;
constexpr int kMaxRecoveriesPerWrite = 3;

std::unique_ptr<AudioPlayer> AlsaPlayer::Create(int sample_rate, int channels) {
    return std::make_unique<AlsaPlayer>(sample_rate, channels);
}

AlsaPlayer::AlsaPlayer(int sample_rate, int channels)
    : AudioPlayer(sample_rate, channels),
      pcm_handle_(nullptr) {}

AlsaPlayer::~AlsaPlayer() { Close(); }

bool AlsaPlayer::Open() {
    if (pcm_handle_) {
        return true;
    }

    int ret = snd_pcm_open(&pcm_handle_, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        ERROR_PRINT("ALSA playout open failed: %s", snd_strerror(ret));
        pcm_handle_ = nullptr;
        return false;
    }

    ret = snd_pcm_set_params(pcm_handle_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             channels_, sample_rate_, 1, kTargetLatencyUs);
    if (ret < 0) {
        ERROR_PRINT("ALSA playout set params failed: %s", snd_strerror(ret));
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
        return false;
    }

    INFO_PRINT("ALSA playout format: S16_LE, %d channels, %d Hz", channels_, sample_rate_);
    return true;
}

void AlsaPlayer::Close() {
    if (pcm_handle_) {
        snd_pcm_drop(pcm_handle_);
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

bool AlsaPlayer::Write(const int16_t *data, size_t frames) {
    if (!pcm_handle_) {
        return false;
    }

    size_t written = 0;
    int recoveries = 0;
    while (written < frames) {
        auto ret = snd_pcm_writei(pcm_handle_, data + written * channels_, frames - written);
        if (ret >= 0) {
            written += static_cast<size_t>(ret);
            continue;
        }
        // Recover from underrun/suspend, but don't spin forever.
        if (++recoveries > kMaxRecoveriesPerWrite ||
            snd_pcm_recover(pcm_handle_, static_cast<int>(ret), 1) < 0) {
            ERROR_PRINT("ALSA playout write failed: %s", snd_strerror(static_cast<int>(ret)));
            return false;
        }
    }
    return true;
}

int AlsaPlayer::DelayMs() {
    if (!pcm_handle_) {
        return 0;
    }

    snd_pcm_sframes_t delay_frames = 0;
    if (snd_pcm_delay(pcm_handle_, &delay_frames) < 0 || delay_frames < 0) {
        return 0;
    }
    return static_cast<int>(delay_frames * 1000 / sample_rate_);
}
