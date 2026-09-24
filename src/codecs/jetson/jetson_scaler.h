#ifndef JETSON_SCALER_H_
#define JETSON_SCALER_H_

#include <functional>
#include <memory>

#include "codecs/frame_processor.h"
#include "common/thread_safe_queue.h"
#include "common/worker.h"

#include <nvbufsurftransform.h>

class JetsonScaler : public IFrameProcessor {
  public:
    struct CaptureTask {
        int dst_dma_fd;
        std::function<void()> callback;
        int64_t queued_us = 0; // only set while latency tracing is on
    };

    static std::unique_ptr<JetsonScaler> Create(ScalerConfig config);

    JetsonScaler(ScalerConfig config);
    ~JetsonScaler() override;

    void EmplaceBuffer(V4L2FrameBufferRef buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override;

  protected:
    bool Initialize() override;
    void CaptureBuffer();
    void Start();

  private:
    ScalerConfig config_;
    int num_buffer_;
    std::atomic<bool> abort_;
    std::unique_ptr<Worker> worker_;
    NvBufSurfTransformRect src_rect_;
    NvBufSurfTransformRect dst_rect_;
    NvBufSurfTransformParams transform_params_;
    ThreadSafeQueue<int> free_buffers_;
    ThreadSafeQueue<CaptureTask> capturing_tasks_;
};

#endif
