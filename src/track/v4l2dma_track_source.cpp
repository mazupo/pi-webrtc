#include "track/v4l2dma_track_source.h"

#if defined(USE_RPI_HW_ENCODER)
#include "codecs/v4l2/v4l2_scaler.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "codecs/jetson/jetson_scaler.h"
#endif
#include "common/latency_tracer.h"
#include "common/logging.h"

webrtc::scoped_refptr<V4L2DmaTrackSource>
V4L2DmaTrackSource::Create(std::shared_ptr<VideoCapturer> capturer) {
#if defined(USE_RPI_HW_ENCODER)
    if (!V4L2Scaler::IsAvailable()) {
        WARN_PRINT("No hardware scaler found; scaling frames in software.");
        return nullptr;
    }
#endif
    auto obj = webrtc::make_ref_counted<V4L2DmaTrackSource>(std::move(capturer));
    obj->StartTrack();
    return obj;
}

V4L2DmaTrackSource::V4L2DmaTrackSource(std::shared_ptr<VideoCapturer> capturer)
    : ScaleTrackSource(capturer),
      is_dma_src_(capturer->is_dma_capture()),
      config_width_(capturer->width()),
      config_height_(capturer->height()) {}

V4L2DmaTrackSource::~V4L2DmaTrackSource() { scaler.reset(); }

void V4L2DmaTrackSource::StartTrack() {
    subscription_ = capturer->Subscribe(
        [this](V4L2FrameBufferRef frame_buffer) {
            OnFrameCaptured(frame_buffer);
        },
        stream_idx);
}

void V4L2DmaTrackSource::OnFrameCaptured(V4L2FrameBufferRef frame_buffer) {
    const int64_t timestamp_us = webrtc::TimeMicros();
    const int64_t translated_timestamp_us =
        timestamp_aligner.TranslateTimestamp(timestamp_us, webrtc::TimeMicros());

    const bool traced = latency::Enabled();
    const int64_t sensor_us = traced ? latency::SensorUs(frame_buffer->timestamp()) : 0;
    if (traced) {
        latency::SetSourceResolution(width, height);
    }
    if (sensor_us != 0) {
        latency::Record(latency::Stage::kSensorToTrackIn, timestamp_us - sensor_us);
    }

    if (capturer->config().no_adaptive) {
        if (traced) {
            latency::SetSentResolution(width, height);
            if (sensor_us != 0) {
                latency::MarkCapture(translated_timestamp_us, sensor_us);
                latency::Record(latency::Stage::kSensorToOnFrame, latency::NowUs() - sensor_us);
            }
        }
        OnFrame(webrtc::VideoFrame::Builder()
                    .set_video_frame_buffer(frame_buffer)
                    .set_rotation(webrtc::kVideoRotation_0)
                    .set_timestamp_us(translated_timestamp_us)
                    .build());
    } else {
        int adapted_width, adapted_height, crop_width, crop_height, crop_x, crop_y;
        if (!AdaptFrame(width, height, timestamp_us, &adapted_width, &adapted_height, &crop_width,
                        &crop_height, &crop_x, &crop_y)) {
            if (traced) {
                latency::Count(latency::Counter::kAdaptDrop);
            }
            return;
        }

        if (traced) {
            latency::SetSentResolution(adapted_width, adapted_height);
        }

        // Full-size YUV dmabufs go straight to the encoder, skipping a scaler copy.
        auto format = frame_buffer->format();
        if (adapted_width == width && adapted_height == height &&
            (format == V4L2_PIX_FMT_YUV420 || format == V4L2_PIX_FMT_NV12) &&
            frame_buffer->GetDmaFd() > 0) {
            scaler.reset();
            if (sensor_us != 0) {
                latency::MarkCapture(translated_timestamp_us, sensor_us);
                latency::Record(latency::Stage::kSensorToOnFrame, latency::NowUs() - sensor_us);
            }
            OnFrame(webrtc::VideoFrame::Builder()
                        .set_video_frame_buffer(frame_buffer)
                        .set_rotation(webrtc::kVideoRotation_0)
                        .set_timestamp_us(translated_timestamp_us)
                        .build());
            return;
        }

        if (!scaler || adapted_width != config_width_ || adapted_height != config_height_) {
            config_width_ = adapted_width;
            config_height_ = adapted_height;
#if defined(USE_RPI_HW_ENCODER)
            scaler = V4L2Scaler::Create({width, height, config_width_, config_height_,
                                         frame_buffer->format(), is_dma_src_, true,
                                         frame_buffer->plane_height()});
#elif defined(USE_JETSON_HW_ENCODER)
            scaler = JetsonScaler::Create({width, height, config_width_, config_height_});
#endif
            DEBUG_PRINT("New scaler is set: %dx%d -> %dx%d", width, height, config_width_,
                        config_height_);
        }
        if (!scaler) {
            return;
        }

        scaler->EmplaceBuffer(frame_buffer, [this, translated_timestamp_us,
                                             sensor_us](V4L2FrameBufferRef scaled_buffer) {
            if (sensor_us != 0) {
                latency::MarkCapture(translated_timestamp_us, sensor_us);
                latency::Record(latency::Stage::kSensorToOnFrame, latency::NowUs() - sensor_us);
            }
            OnFrame(webrtc::VideoFrame::Builder()
                        .set_video_frame_buffer(scaled_buffer)
                        .set_rotation(webrtc::kVideoRotation_0)
                        .set_timestamp_us(translated_timestamp_us)
                        .build());
        });
    }
}
