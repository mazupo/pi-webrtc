#ifndef JETSON_CODEC_
#define JETSON_CODEC_

#include "codecs/frame_processor.h"
#include "common/thread_safe_queue.h"
#include "common/v4l2_frame_buffer.h"

#include <NvVideoEncoder.h>

class JetsonEncoder : public FrameProcessor {
  public:
    static std::unique_ptr<JetsonEncoder> Create(EncoderConfig config);

    JetsonEncoder(EncoderConfig config, const char *name);
    ~JetsonEncoder() override;

    void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override;
    void ForceKeyFrame();
    void SetFps(int adjusted_fps);
    void SetBitrate(int adjusted_bitrate_bps);

  protected:
    bool Initialize() override;

  private:
    NvVideoEncoder *encoder_;
    std::atomic<bool> abort_;
    const char *name_;
    EncoderConfig config_;
    ThreadSafeQueue<std::function<void(V4L2FrameBufferRef)>> capturing_tasks_;

    bool CreateVideoEncoder();
    bool PrepareCaptureBuffer();
    void Start();
    void SendEOS();
    static bool EncoderCapturePlaneDqCallback(struct v4l2_buffer *v4l2_buf, NvBuffer *buffer,
                                              NvBuffer *shared_buffer, void *arg);
    void ConvertI420ToYUV420M(NvBuffer *nv_buffer,
                              webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer);
    uint32_t DisableAV1IVF();
};

#endif
