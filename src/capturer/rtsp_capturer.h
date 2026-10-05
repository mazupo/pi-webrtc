#ifndef RTSP_CAPTURER_H_
#define RTSP_CAPTURER_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

#include "args.h"
#include "capturer/video_capturer.h"
#include "codecs/frame_processor.h"
#include "common/interface/subject.h"
#include "common/v4l2_frame_buffer.h"
#include "common/worker.h"

class RtspCapturer : public VideoCapturer {
  public:
    static std::shared_ptr<RtspCapturer> Create(Args args);

    RtspCapturer(Args args);
    ~RtspCapturer() override;

    int fps() const override;
    int width(int stream_idx = 0) const override;
    int height(int stream_idx = 0) const override;
    bool is_dma_capture() const override;
    uint32_t format() const override;
    Args config() const override;
    void StartCapture() override;

    webrtc::scoped_refptr<webrtc::I420BufferInterface> GetI420Frame(int stream_idx = 0) override;
    Subscription Subscribe(Subject<V4L2FrameBufferRef>::Callback callback,
                           int stream_idx = 0) override;

  private:
    struct StreamInfo {
        uint32_t format = 0;
        int width = 0;
        int height = 0;
        int fps = 0;
        std::vector<uint8_t> parameter_sets;
    };

    Args config_;
    std::string url_;
    StreamInfo stream_;
    bool hw_decoder_;
    bool has_first_keyframe_;
    bool retrying_;
    int stream_index_;
    AVFormatContext *fmt_ctx_;
    AVPacket *packet_;
    std::atomic<bool> stopping_;
    std::mutex retry_mtx_;
    std::condition_variable retry_cv_;
    std::unique_ptr<IFrameProcessor> decoder_;
    std::mutex frame_mtx_;
    V4L2FrameBufferRef frame_buffer_;
    Subject<V4L2FrameBufferRef> stream_subject_;
    std::unique_ptr<Worker> worker_;

    void Initialize();
    bool Open(StreamInfo *info);
    void Close();
    void Reconnect();
    void CreateDecoder();
    void WaitToRetry();
    void ReadPacket();
    void OnFrame(V4L2FrameBufferRef frame_buffer, timeval timestamp);
    void SetFrameBuffer(V4L2FrameBufferRef frame_buffer);
    static int Interrupt(void *opaque);
};

#endif // RTSP_CAPTURER_H_
