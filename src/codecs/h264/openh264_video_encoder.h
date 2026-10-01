#ifndef OPENH264_VIDEO_ENCODER_H_
#define OPENH264_VIDEO_ENCODER_H_

#include <atomic>
#include <optional>

// WebRTC
#include <api/video/video_timing.h>
#include <api/video_codecs/video_encoder.h>
#include <common_video/h264/h264_bitstream_parser.h>
#include <modules/video_coding/codecs/h264/include/h264.h>

#include "args.h"
#include "codecs/h264/openh264_encoder.h"

class Openh264VideoEncoder : public webrtc::VideoEncoder {
  public:
    static std::unique_ptr<webrtc::VideoEncoder> Create(Args args);
    Openh264VideoEncoder(Args args);

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
    int target_fps_;
    int target_bitrate_bps_;
    int num_temporal_layers_;
    int tl0sync_limit_;
    int number_of_cores_;
    std::optional<int> encoder_thread_limit_;
    std::optional<webrtc::VideoPlayoutDelay> playout_delay_;
    webrtc::VideoCodec codec_;
    webrtc::EncodedImage encoded_image_;
    webrtc::H264BitstreamParser bitstream_parser_;
    std::atomic<webrtc::EncodedImageCallback *> callback_;
    std::unique_ptr<Openh264Encoder> encoder_;

    void SendFrame(const webrtc::VideoFrame &frame, const SFrameBSInfo &info);
};

#endif // OPENH264_VIDEO_ENCODER_H_
