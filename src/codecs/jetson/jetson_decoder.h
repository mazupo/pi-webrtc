#ifndef JETSON_DECODER_H_
#define JETSON_DECODER_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "codecs/frame_processor.h"
#include "common/thread_safe_queue.h"
#include "common/v4l2_frame_buffer.h"

#include <NvVideoDecoder.h>
#include <nvbufsurftransform.h>

class JetsonDecoder : public IFrameProcessor {
  public:
    static std::unique_ptr<JetsonDecoder> Create(DecoderConfig config);

    JetsonDecoder(DecoderConfig config, std::string name);
    ~JetsonDecoder() override;

    void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override;

  protected:
    bool Initialize() override;

  private:
    NvVideoDecoder *decoder_;
    std::string name_;
    DecoderConfig config_;
    uint32_t frame_size_;
    std::atomic<bool> abort_;
    std::atomic<bool> capture_ready_;
    NvBufSurfTransformRect src_rect_;
    NvBufSurfTransformRect dst_rect_;
    NvBufSurfTransformParams transform_params_;
    ThreadSafeQueue<int> free_buffers_;
    ThreadSafeQueue<std::function<void(V4L2FrameBufferRef)>> capturing_tasks_;

    bool CreateVideoDecoder();
    bool AllocateFrameBuffers();
    void Start();
    bool EnsureCapturePlane();
    bool PrepareCaptureBuffer();
    void Transform(int src_dma_fd, const std::function<void(V4L2FrameBufferRef)> &on_capture);
    void SendEOS();
    static bool CapturePlaneDqCallback(struct v4l2_buffer *v4l2_buf, NvBuffer *buffer,
                                       NvBuffer *shared_buffer, void *arg);
};

#endif // JETSON_DECODER_H_
