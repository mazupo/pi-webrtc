#ifndef PA_CAPTURER_H_
#define PA_CAPTURER_H_

#include <vector>

#include <pulse/error.h>
#include <pulse/simple.h>

#include "capturer/audio_capturer.h"
#include "capturer/capture_config.h"

class PaCapturer : public AudioCapturer {
  public:
    static std::shared_ptr<AudioCapturer> Create(AudioCaptureConfig config);

    PaCapturer(AudioCaptureConfig config);
    ~PaCapturer();

  protected:
    void StartCapture() override;

  private:
    pa_simple *src;
    std::vector<float> capture_buf_;

    void CaptureSamples();
    bool CreateFloat32Source();
};

#endif
