#include "codecs/ffmpeg/ffmpeg_decoder.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include "common/logging.h"
#include "common/v4l2_utils.h"

namespace {

AVCodecID ToCodecId(uint32_t pix_fmt) {
    switch (pix_fmt) {
        case V4L2_PIX_FMT_H264:
            return AV_CODEC_ID_H264;
        case V4L2_PIX_FMT_HEVC:
            return AV_CODEC_ID_HEVC;
        default:
            return AV_CODEC_ID_NONE;
    }
}

AVPixelFormat GetDrmFormat(AVCodecContext *ctx, const AVPixelFormat *formats) {
    for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_DRM_PRIME) {
            return *p;
        }
    }
    return avcodec_default_get_format(ctx, formats);
}

} // namespace

std::unique_ptr<FfmpegDecoder> FfmpegDecoder::Create(DecoderConfig config, bool try_hw) {
    auto decoder = std::make_unique<FfmpegDecoder>(config, try_hw);
    if (!decoder->Initialize()) {
        return nullptr;
    }
    return decoder;
}

FfmpegDecoder::FfmpegDecoder(DecoderConfig config, bool try_hw)
    : config_(config),
      try_hw_(try_hw),
      codec_ctx_(nullptr),
      hw_device_ctx_(nullptr),
      packet_(nullptr),
      frame_(nullptr),
      sw_frame_(nullptr),
      format_reported_(false) {}

FfmpegDecoder::~FfmpegDecoder() {
    av_frame_free(&sw_frame_);
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&codec_ctx_);
    av_buffer_unref(&hw_device_ctx_);
}

bool FfmpegDecoder::Initialize() {
    const AVCodec *codec = avcodec_find_decoder(ToCodecId(config_.src_pix_fmt));
    if (!codec) {
        ERROR_PRINT("No libavcodec decoder for %s",
                    v4l2_util::FourccToString(config_.src_pix_fmt).c_str());
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return false;
    }
    codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // Slice threads add no delay; two let a multi-slice stream use a second core.
    codec_ctx_->thread_type = FF_THREAD_SLICE;
    codec_ctx_->thread_count = 2;

    bool hw = try_hw_ && UseDrmHwaccel(codec);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        ERROR_PRINT("Could not open the %s decoder", codec->name);
        return false;
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    sw_frame_ = av_frame_alloc();
    if (!packet_ || !frame_ || !sw_frame_) {
        return false;
    }

    INFO_PRINT("Decoding %s with libavcodec (%s).",
               v4l2_util::FourccToString(config_.src_pix_fmt).c_str(),
               hw ? "DRM hwaccel" : "software");
    return true;
}

bool FfmpegDecoder::UseDrmHwaccel(const AVCodec *codec) {
    bool offered = false;
    for (int i = 0; const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i); i++) {
        if (config->device_type == AV_HWDEVICE_TYPE_DRM &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            offered = true;
            break;
        }
    }
    if (!offered ||
        av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_DRM, nullptr, nullptr, 0) < 0) {
        return false;
    }
    codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    codec_ctx_->get_format = GetDrmFormat;
    return true;
}

void FfmpegDecoder::EmplaceBuffer(V4L2FrameBufferRef frame_buffer,
                                  std::function<void(V4L2FrameBufferRef)> on_capture) {
    // libavcodec copies a packet that does not own its data.
    packet_->data = static_cast<uint8_t *>(const_cast<void *>(frame_buffer->Data()));
    packet_->size = frame_buffer->size();
    int ret = avcodec_send_packet(codec_ctx_, packet_);
    av_packet_unref(packet_);
    if (ret < 0) {
        DEBUG_PRINT("avcodec_send_packet failed: %d", ret);
        return;
    }

    while (avcodec_receive_frame(codec_ctx_, frame_) == 0) {
        if (frame_->format == AV_PIX_FMT_DRM_PRIME) {
            if (av_hwframe_transfer_data(sw_frame_, frame_, 0) == 0) {
                Output(sw_frame_, on_capture);
            }
            av_frame_unref(sw_frame_);
        } else {
            Output(frame_, on_capture);
        }
        av_frame_unref(frame_);
    }
}

void FfmpegDecoder::Output(const AVFrame *frame,
                           const std::function<void(V4L2FrameBufferRef)> &on_capture) {
    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
        if (!format_reported_) {
            ERROR_PRINT("Decoded frames are %s; only 8-bit 4:2:0 is supported.",
                        av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
            format_reported_ = true;
        }
        return;
    }

    int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, frame->width, frame->height, 1);
    auto frame_buffer =
        V4L2FrameBuffer::Create(frame->width, frame->height, size, V4L2_PIX_FMT_YUV420);
    av_image_copy_to_buffer(frame_buffer->MutableData(), size, frame->data, frame->linesize,
                            AV_PIX_FMT_YUV420P, frame->width, frame->height, 1);
    on_capture(frame_buffer);
}
