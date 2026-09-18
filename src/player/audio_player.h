#ifndef AUDIO_PLAYER_H_
#define AUDIO_PLAYER_H_

#include <cstddef>
#include <cstdint>

// Interleaved S16 playout sink.
class AudioPlayer {
  public:
    AudioPlayer(int sample_rate, int channels)
        : sample_rate_(sample_rate),
          channels_(channels) {}
    virtual ~AudioPlayer() = default;

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

    virtual bool Open() = 0;
    virtual void Close() = 0;
    // Blocking, so it paces the playout thread.
    virtual bool Write(const int16_t *data, size_t frames) = 0;
    virtual int DelayMs() = 0;

  protected:
    int sample_rate_;
    int channels_;
};

#endif // AUDIO_PLAYER_H_
