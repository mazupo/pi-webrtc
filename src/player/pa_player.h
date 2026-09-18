#ifndef PA_PLAYER_H_
#define PA_PLAYER_H_

#include <memory>

#include <pulse/error.h>
#include <pulse/simple.h>

#include "player/audio_player.h"

class PaPlayer : public AudioPlayer {
  public:
    static std::unique_ptr<AudioPlayer> Create(int sample_rate, int channels);

    PaPlayer(int sample_rate, int channels);
    ~PaPlayer() override;

    bool Open() override;
    void Close() override;
    bool Write(const int16_t *data, size_t frames) override;
    int DelayMs() override;

  private:
    pa_simple *sink_;
};

#endif // PA_PLAYER_H_
