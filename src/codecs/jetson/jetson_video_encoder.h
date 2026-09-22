#ifndef JETSON_VIDEO_ENCODER_H_
#define JETSON_VIDEO_ENCODER_H_

// WebRTC
#include <api/video/video_timing.h>
#include <api/video_codecs/video_encoder.h>
#include <common_video/include/bitrate_adjuster.h>

#include <atomic>
#include <optional>

#include "args.h"
#include "codecs/jetson/jetson_encoder.h"
#include "common/v4l2_utils.h"

class JetsonVideoEncoder : public webrtc::VideoEncoder {
  public:
    static std::unique_ptr<webrtc::VideoEncoder> Create(Args args);
    JetsonVideoEncoder(Args args);

    int32_t InitEncode(const webrtc::VideoCodec *codec_settings,
                       const VideoEncoder::Settings &settings) override;
    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) override;
    int32_t Release() override;
    int32_t Encode(const webrtc::VideoFrame &frame,
                   const std::vector<webrtc::VideoFrameType> *frame_types) override;
    void SetRates(const RateControlParameters &parameters) override;
    webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

  protected:
    int width_;
    int height_;
    int fps_adjuster_;
    bool is_dma_;
    std::string name_;
    webrtc::VideoCodec codec_;
    webrtc::EncodedImage encoded_image_;
    std::atomic<webrtc::EncodedImageCallback *> callback_;
    webrtc::BitrateAdjuster bitrate_adjuster_;
    std::optional<webrtc::VideoPlayoutDelay> playout_delay_;
    std::unique_ptr<JetsonEncoder> encoder_;

    virtual void SendFrame(const webrtc::VideoFrame &frame, V4L2Buffer &encoded_buffer);

  private:
    static uint32_t GetV4L2CodecFormat(webrtc::VideoCodecType codec);
};

#endif
