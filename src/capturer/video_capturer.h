#ifndef VIDEO_CAPTURER_H_
#define VIDEO_CAPTURER_H_

#include "capturer/frame_source.h"

// A frame source backed by a camera or stream, which can be started and controlled.
class VideoCapturer : public FrameSource {
  public:
    virtual void StartCapture() = 0;
    virtual bool SetControls(int key, int value) { return false; };
};

#endif
