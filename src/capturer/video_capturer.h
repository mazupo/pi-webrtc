#ifndef VIDEO_CAPTURER_H_
#define VIDEO_CAPTURER_H_

#include "capturer/frame_source.h"

// A frame source backed by a camera or stream; it starts on creation and can be controlled.
class VideoCapturer : public FrameSource {
  public:
    virtual bool SetControls(int key, int value) { return false; };

  protected:
    virtual void StartCapture() = 0;
};

#endif
