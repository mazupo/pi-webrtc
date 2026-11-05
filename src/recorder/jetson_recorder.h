#ifndef JETSON_RECORDER_H_
#define JETSON_RECORDER_H_

#include "codecs/jetson/jetson_encoder.h"
#include "recorder/video_recorder.h"

class JetsonRecorder : public VideoRecorder {
  public:
    static std::unique_ptr<JetsonRecorder> Create(int width, int height, int fps);
    JetsonRecorder(int width, int height, int fps);

  protected:
    void ReleaseEncoder() override;
    void Encode(V4L2FrameBufferRef frame_buffer) override;

  private:
    std::unique_ptr<JetsonEncoder> encoder_;
};

#endif
