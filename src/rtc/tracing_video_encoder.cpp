#include "rtc/tracing_video_encoder.h"

#include <utility>

#include "common/latency_tracer.h"

#include <modules/video_coding/include/video_error_codes.h>
#include <rtc_base/bitrate_tracker.h>

namespace {

// A capture sequence that jumps backwards means the side table evicted the entry rather than that
// frames went missing, so gaps beyond a few seconds of video are ignored instead of counted.
constexpr uint32_t kMaxPlausibleGap = 1000;

// Sits between the encoder and WebRTC so both ends of an asynchronous encode are visible from one
// place: OnEncodedImage runs on the encoder's dequeue thread, long after Encode() returned.
class TracingEncodedImageCallback : public webrtc::EncodedImageCallback {
  public:
    void SetTarget(webrtc::EncodedImageCallback *target) { target_ = target; }

    Result OnEncodedImage(const webrtc::EncodedImage &encoded_image,
                          const webrtc::CodecSpecificInfo *codec_specific_info) override {
        if (!target_) {
            return Result(Result::ERROR_SEND_FAILED);
        }

        if (!latency::Enabled()) {
            return target_->OnEncodedImage(encoded_image, codec_specific_info);
        }

        const int64_t sensor_us = latency::LookupEncode(encoded_image.RtpTimestamp());
        const int64_t encoded_us = latency::NowUs();
        if (sensor_us != 0) {
            latency::Record(latency::Stage::kSensorToEncoded, encoded_us - sensor_us);
        }

        auto result = target_->OnEncodedImage(encoded_image, codec_specific_info);

        const int64_t sent_us = latency::NowUs();
        latency::Record(latency::Stage::kOnEncodedImage, sent_us - encoded_us);
        if (sensor_us != 0) {
            latency::Record(latency::Stage::kSensorToSent, sent_us - sensor_us);
        }
        latency::Count(latency::Counter::kFramesEncoded);

        const webrtc::Timestamp now = webrtc::Timestamp::Micros(sent_us);
        produced_.Update(encoded_image.size(), now);
        latency::SetProducedKbps(produced_.Rate(now).value_or(webrtc::DataRate::Zero()).kbps());

        return result;
    }

    void OnDroppedFrame(DropReason reason) override {
        if (target_) {
            target_->OnDroppedFrame(reason);
        }
    }

  private:
    webrtc::EncodedImageCallback *target_ = nullptr;
    // Touched only from OnEncodedImage, so it needs no lock.
    webrtc::BitrateTracker produced_{webrtc::TimeDelta::Millis(1500)};
};

class TracingVideoEncoder : public webrtc::VideoEncoder {
  public:
    explicit TracingVideoEncoder(std::unique_ptr<webrtc::VideoEncoder> encoder)
        : encoder_(std::move(encoder)) {}

    void SetFecControllerOverride(webrtc::FecControllerOverride *fec_controller_override) override {
        encoder_->SetFecControllerOverride(fec_controller_override);
    }

    int InitEncode(const webrtc::VideoCodec *codec_settings,
                   const VideoEncoder::Settings &settings) override {
        return encoder_->InitEncode(codec_settings, settings);
    }

    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback *callback) override {
        if (!callback) {
            return encoder_->RegisterEncodeCompleteCallback(nullptr);
        }
        callback_.SetTarget(callback);
        return encoder_->RegisterEncodeCompleteCallback(&callback_);
    }

    int32_t Release() override {
        int32_t result = encoder_->Release();
        callback_.SetTarget(nullptr);
        return result;
    }

    int32_t Encode(const webrtc::VideoFrame &frame,
                   const std::vector<webrtc::VideoFrameType> *frame_types) override {
        if (!latency::Enabled()) {
            return encoder_->Encode(frame, frame_types);
        }

        // The track source stashed the sensor timestamp under timestamp_us(), which
        // VideoStreamEncoder passes through untouched. Re-key it on the RTP timestamp, the only
        // field the resulting EncodedImage shares with this frame.
        const latency::CaptureInfo capture = latency::LookupCapture(frame.timestamp_us());
        const int64_t sensor_us = capture.sensor_us;
        const int64_t encode_start_us = latency::NowUs();
        if (sensor_us != 0) {
            latency::Record(latency::Stage::kSensorToEncodeIn, encode_start_us - sensor_us);
            latency::MarkEncode(frame.rtp_timestamp(), sensor_us);
        }

        // VideoStreamEncoder drops frames on its own account -- the frame dropper when the output
        // overshoots the target bitrate, the encoder queue when the previous frame is still in
        // flight -- and reports them only to its stats observer, which is out of reach from here.
        // The frames it kept arrive in capture order, so whatever is missing from the sequence is
        // exactly what it dropped. Counted per encoder instance, and every peer connection has one
        // of its own, so this adds up the same way the encoded frame count does.
        if (capture.seq != 0) {
            const uint32_t gap = capture.seq - last_capture_seq_ - 1; // unsigned: a wrap is fine
            if (last_capture_seq_ != 0 && gap != 0 && gap < kMaxPlausibleGap) {
                latency::Count(latency::Counter::kEncoderQueueDrop, gap);
            }
            last_capture_seq_ = capture.seq;
        }

        int32_t result = encoder_->Encode(frame, frame_types);

        latency::RecordSince(latency::Stage::kEncodeCall, encode_start_us);
        return result;
    }

    void SetRates(const RateControlParameters &parameters) override {
        if (latency::Enabled()) {
            latency::SetBitrateKbps(parameters.target_bitrate.get_sum_kbps(),
                                    parameters.bitrate.get_sum_kbps());
        }
        encoder_->SetRates(parameters);
    }

    void OnPacketLossRateUpdate(float packet_loss_rate) override {
        encoder_->OnPacketLossRateUpdate(packet_loss_rate);
    }

    void OnRttUpdate(int64_t rtt_ms) override { encoder_->OnRttUpdate(rtt_ms); }

    void OnLossNotification(const LossNotification &loss_notification) override {
        encoder_->OnLossNotification(loss_notification);
    }

    EncoderInfo GetEncoderInfo() const override { return encoder_->GetEncoderInfo(); }

  private:
    std::unique_ptr<webrtc::VideoEncoder> encoder_;
    TracingEncodedImageCallback callback_;
    // Encode() runs on the encoder's own sequenced queue, so this needs no synchronisation.
    uint32_t last_capture_seq_ = 0;
};

} // namespace

std::unique_ptr<webrtc::VideoEncoder>
CreateTracingVideoEncoder(std::unique_ptr<webrtc::VideoEncoder> encoder) {
    if (!encoder) {
        return nullptr;
    }
    return std::make_unique<TracingVideoEncoder>(std::move(encoder));
}
