#ifndef OPENH264_ENCODER_
#define OPENH264_ENCODER_

#include <cstdint>
#include <functional>

#include <api/video/i420_buffer.h>
#include <third_party/openh264/src/codec/api/wels/codec_api.h>

#include "codecs/frame_processor.h"

class Openh264Encoder {
  public:
    static std::unique_ptr<Openh264Encoder> Create(EncoderConfig config);
    Openh264Encoder(EncoderConfig config);
    ~Openh264Encoder();
    bool Init();
    bool Encode(webrtc::scoped_refptr<webrtc::I420BufferInterface> frame_buffer,
                std::function<void(uint8_t *, int, bool is_keyframe)> on_capture);
    void ForceIntraFrame();
    void SetRates(int bitrate_bps, float fps);

  private:
    EncoderConfig config_;
    int max_bitrate_;
    ISVCEncoder *encoder_;
    SSourcePicture src_pic_;
    std::vector<uint8_t> encoded_buf_;
};

#endif // OPENH264_ENCODER_
