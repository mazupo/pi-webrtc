#ifndef SCALE_TRACK_SOURCE_H_
#define SCALE_TRACK_SOURCE_H_

#include <media/base/adapted_video_track_source.h>
#include <rtc_base/timestamp_aligner.h>

#include "capturer/frame_source.h"

class ScaleTrackSource : public webrtc::AdaptedVideoTrackSource {
  public:
    static webrtc::scoped_refptr<ScaleTrackSource> Create(std::shared_ptr<FrameSource> source,
                                                          int stream_idx, bool no_adaptive);
    ScaleTrackSource(std::shared_ptr<FrameSource> source, int stream_idx, bool no_adaptive);
    ~ScaleTrackSource();

    SourceState state() const override;
    bool remote() const override;
    bool is_screencast() const override;
    std::optional<bool> needs_denoising() const override;
    virtual void StartTrack();

  protected:
    int width;
    int height;
    int stream_idx;
    bool no_adaptive;
    std::shared_ptr<FrameSource> source;
    webrtc::TimestampAligner timestamp_aligner;

  private:
    Subscription subscription_;
    void OnFrameCaptured(webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer);
};

#endif
