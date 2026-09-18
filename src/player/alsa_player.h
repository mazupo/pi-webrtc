#ifndef ALSA_PLAYER_H_
#define ALSA_PLAYER_H_

#include <memory>

#include <alsa/asoundlib.h>

#include "player/audio_player.h"

class AlsaPlayer : public AudioPlayer {
  public:
    static std::unique_ptr<AudioPlayer> Create(int sample_rate, int channels);

    AlsaPlayer(int sample_rate, int channels);
    ~AlsaPlayer() override;

    bool Open() override;
    void Close() override;
    bool Write(const int16_t *data, size_t frames) override;
    int DelayMs() override;

  private:
    snd_pcm_t *pcm_handle_;
};

#endif // ALSA_PLAYER_H_
