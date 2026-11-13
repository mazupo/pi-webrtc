#ifndef HW_SCALE_TRACK_SOURCE_H_
#define HW_SCALE_TRACK_SOURCE_H_

#include "codecs/frame_processor.h"
#include "track/scale_track_source.h"

class HwScaleTrackSource : public ScaleTrackSource {
  public:
    static webrtc::scoped_refptr<HwScaleTrackSource> Create(std::shared_ptr<FrameSource> source,
                                                            int stream_idx, bool no_adaptive);
    HwScaleTrackSource(std::shared_ptr<FrameSource> source, int stream_idx, bool no_adaptive);
    ~HwScaleTrackSource();
    void StartTrack() override;

  private:
    bool is_dma_src_;
    int config_width_;
    int config_height_;
    Subscription subscription_;
    std::unique_ptr<FrameProcessor> scaler;

    void OnFrameCaptured(V4L2FrameBufferRef frame_buffer);
};

#endif
