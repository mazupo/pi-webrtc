#ifndef FRAME_PROCESSOR_H_
#define FRAME_PROCESSOR_H_

#include <functional>

#include <linux/videodev2.h>

#include "common/v4l2_frame_buffer.h"

struct EncoderConfig {
    int width;
    int height;
    int fps = 30;
    int bitrate = 2 * 1024 * 1024;
    int max_bitrate = 0;
    int keyframe_interval = 600;
    int idr_interval = 256;
    bool frame_dropping = true;
    int thread_count = 4;
    bool is_dma_src = false;
    uint32_t src_pix_fmt = 0;
    uint32_t dst_pix_fmt = 0;
    v4l2_mpeg_video_bitrate_mode rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
};

struct ScalerConfig {
    int src_width;
    int src_height;
    int dst_width;
    int dst_height;
    uint32_t src_pix_fmt = 0;
    bool is_dma_src = false;
    bool is_dma_dst = false;
};

struct DecoderConfig {
    int width;
    int height;
    uint32_t src_pix_fmt = 0;
    bool is_dma_dst = false;
};

class IFrameProcessor {
  public:
    virtual ~IFrameProcessor() = default;

    /**
     * Submits a frame buffer to a processing unit (e.g., ISP, encoder).
     *
     * This method enqueues the given frame buffer into the hardware pipeline. The callback will be
     * triggered with the resulting buffer when processing is completed.
     *
     * @param frame_buffer Frame buffer to be processed by the device.
     * @param on_capture Callback invoked with the resulting frame buffer.
     */
    virtual void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                               std::function<void(V4L2FrameBufferRef)> on_capture) = 0;

  protected:
    virtual bool Initialize() = 0;
};

#endif
