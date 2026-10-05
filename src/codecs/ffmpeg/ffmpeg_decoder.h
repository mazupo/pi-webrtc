#ifndef FFMPEG_DECODER_H_
#define FFMPEG_DECODER_H_

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "codecs/frame_processor.h"

// Decodes H264/H265 with libavcodec into I420 frames, using the DRM hwaccel when asked and offered.
class FfmpegDecoder : public IFrameProcessor {
  public:
    static std::unique_ptr<FfmpegDecoder> Create(DecoderConfig config, bool try_hw);

    FfmpegDecoder(DecoderConfig config, bool try_hw);
    ~FfmpegDecoder() override;

    void EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                       std::function<void(V4L2FrameBufferRef)> on_capture) override;

  protected:
    bool Initialize() override;

  private:
    DecoderConfig config_;
    bool try_hw_;
    AVCodecContext *codec_ctx_;
    AVBufferRef *hw_device_ctx_;
    AVPacket *packet_;
    AVFrame *frame_;
    AVFrame *sw_frame_;
    bool format_reported_;

    bool UseDrmHwaccel(const AVCodec *codec);
    void Output(const AVFrame *frame, const std::function<void(V4L2FrameBufferRef)> &on_capture);
};

#endif // FFMPEG_DECODER_H_
