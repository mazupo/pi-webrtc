#include "track/scale_track_source.h"

#include "common/latency_tracer.h"
#include "common/v4l2_frame_buffer.h"

// WebRTC
#include <api/video/i420_buffer.h>
#include <third_party/libyuv/include/libyuv.h>

static const int kBufferAlignment = 64;

namespace {

// Both capturers stamp the buffer with the sensor time, but only V4L2FrameBuffer carries it.
int64_t SensorUsOf(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer> &frame_buffer) {
    if (!frame_buffer || frame_buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
        return 0;
    }
    auto *v4l2_buffer = static_cast<V4L2FrameBuffer *>(frame_buffer.get());
    return latency::SensorUs(v4l2_buffer->timestamp());
}

} // namespace

webrtc::scoped_refptr<ScaleTrackSource>
ScaleTrackSource::Create(std::shared_ptr<FrameSource> source, int stream_idx, bool no_adaptive) {
    auto obj =
        webrtc::make_ref_counted<ScaleTrackSource>(std::move(source), stream_idx, no_adaptive);
    obj->StartTrack();
    return obj;
}

ScaleTrackSource::ScaleTrackSource(std::shared_ptr<FrameSource> source, int stream_idx,
                                   bool no_adaptive)
    : width(source->width()),
      height(source->height()),
      stream_idx(stream_idx),
      no_adaptive(no_adaptive),
      source(source) {}

ScaleTrackSource::~ScaleTrackSource() {
    // todo: tell capture unsubscribe observer.
}

void ScaleTrackSource::StartTrack() {
    subscription_ = source->Subscribe(
        [this](webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer) {
            OnFrameCaptured(frame_buffer);
        },
        stream_idx);
}

void ScaleTrackSource::OnFrameCaptured(
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer) {
    const int64_t timestamp_us = webrtc::TimeMicros();
    const int64_t translated_timestamp_us =
        timestamp_aligner.TranslateTimestamp(timestamp_us, webrtc::TimeMicros());

    const bool traced = latency::Enabled();
    const int64_t sensor_us = traced ? SensorUsOf(frame_buffer) : 0;
    if (traced) {
        latency::SetSourceResolution(width, height);
    }
    if (sensor_us != 0) {
        latency::Record(latency::Stage::kSensorToTrackIn, timestamp_us - sensor_us);
    }

    int adapted_width, adapted_height, crop_width, crop_height, crop_x, crop_y;
    if (no_adaptive) {
        adapted_width = width;
        adapted_height = height;
    } else if (!AdaptFrame(width, height, timestamp_us, &adapted_width, &adapted_height,
                           &crop_width, &crop_height, &crop_x, &crop_y)) {
        if (traced) {
            latency::Count(latency::Counter::kAdaptDrop);
        }
        return;
    }

    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> dst_buffer = frame_buffer;

    if (adapted_width != width || adapted_height != height) {
        const int64_t scale_start_us = traced ? latency::NowUs() : 0;
        int dst_stride = std::ceil((double)adapted_width / kBufferAlignment) * kBufferAlignment;
        auto i420_buffer = webrtc::I420Buffer::Create(adapted_width, adapted_height, dst_stride,
                                                      dst_stride / 2, dst_stride / 2);
        i420_buffer->ScaleFrom(*frame_buffer->ToI420());
        dst_buffer = i420_buffer;
        if (traced) {
            latency::RecordSince(latency::Stage::kI420Scale, scale_start_us);
        }
    }

    if (traced) {
        latency::SetSentResolution(adapted_width, adapted_height);
        if (sensor_us != 0) {
            latency::MarkCapture(translated_timestamp_us, sensor_us);
            latency::Record(latency::Stage::kSensorToOnFrame, latency::NowUs() - sensor_us);
        }
    }

    OnFrame(webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(dst_buffer)
                .set_rotation(webrtc::kVideoRotation_0)
                .set_timestamp_us(translated_timestamp_us)
                .build());
}

webrtc::MediaSourceInterface::SourceState ScaleTrackSource::state() const {
    return SourceState::kLive;
}

bool ScaleTrackSource::remote() const { return false; }

bool ScaleTrackSource::is_screencast() const { return false; }

std::optional<bool> ScaleTrackSource::needs_denoising() const { return false; }
