#ifndef VIDEO_RECORDER_H_
#define VIDEO_RECORDER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "codecs/frame_processor.h"
#include "common/thread_safe_queue.h"
#include "common/v4l2_frame_buffer.h"
#include "recorder/recorder.h"

class VideoRecorder : public Recorder<V4L2FrameBufferRef> {
  public:
    // Records with the hardware encoder when hw_accel is set and the device has one.
    static std::unique_ptr<VideoRecorder> Create(int width, int height, int fps, bool hw_accel);
    VideoRecorder(int width, int height, int fps, bool use_hw);
    void OnBuffer(V4L2FrameBufferRef buffer) override;
    void OnStart() override final;

  protected:
    bool ConsumeBuffer() override;

  private:
    int fps;
    int width;
    int height;
    bool use_hw_;
    uint32_t format_;
    ThreadSafeQueue<V4L2FrameBufferRef> frame_buffer_queue;
    std::mutex encoder_mtx_;
    struct timeval base_time_;
    std::atomic<bool> base_time_initialized;
    std::unique_ptr<FrameProcessor> encoder_;

    void InitializeEncoderCtx(AVCodecContext *&encoder) override;
    void Encode(V4L2FrameBufferRef frame_buffer);
    void OnEncoded(uint8_t *start, uint32_t length, timeval timestamp, uint32_t flags = 0);
    bool IsEncoderReady();
};

#endif // VIDEO_RECORDER_H_
