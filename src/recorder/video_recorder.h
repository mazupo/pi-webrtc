#ifndef VIDEO_RECORDER_H_
#define VIDEO_RECORDER_H_

#include <atomic>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "common/thread_safe_queue.h"
#include "common/v4l2_frame_buffer.h"
#include "recorder/recorder.h"

class VideoRecorder : public Recorder<V4L2FrameBufferRef> {
  public:
    VideoRecorder(int width, int height, int fps, AVCodecID encoder_id);
    virtual ~VideoRecorder() {};
    void OnBuffer(V4L2FrameBufferRef buffer) override;
    void OnStart() override final;

  protected:
    int fps;
    int width;
    int height;
    AVCodecID encoder_id;
    ThreadSafeQueue<V4L2FrameBufferRef> frame_buffer_queue;

    virtual void ReleaseEncoder() = 0;
    virtual void Encode(V4L2FrameBufferRef frame_buffer) = 0;

    bool ConsumeBuffer() override;
    void OnEncoded(uint8_t *start, uint32_t length, timeval timestamp, uint32_t flags = 0);
    bool IsEncoderReady();

  private:
    std::mutex encoder_mtx_;
    struct timeval base_time_;
    std::atomic<bool> base_time_initialized;

    void InitializeEncoderCtx(AVCodecContext *&encoder) override;
};

#endif // VIDEO_RECORDER_H_
