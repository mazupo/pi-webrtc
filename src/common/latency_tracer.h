#ifndef COMMON_LATENCY_TRACER_H_
#define COMMON_LATENCY_TRACER_H_

#include <atomic>
#include <cstdint>
#include <sys/time.h>

// Per-frame stage timing for the capture -> scale -> encode -> send pipeline.
//
// Every hook is guarded by latency::Enabled(), a relaxed atomic load, so a release build with the
// tracer off pays one load and a predictable branch per hook. When it is on, a sample is a single
// relaxed fetch_add into a fixed histogram: no allocation, no lock, nothing that can block the
// libcamera or argus thread. A reporter thread prints p50/p95/max once per interval.
namespace latency {

// Cumulative stages are measured from the sensor timestamp, so they can be read as a latency
// budget. The rest are per-stage costs, useful for attributing whichever cumulative step jumps.
enum class Stage : int {
    kSensorToCapture = 0, // exposure done -> capture callback (ISP + driver + kernel -> userspace)
    kSensorToTrackIn,     // -> ScaleTrackSource/V4L2DmaTrackSource::OnFrameCaptured entry
    kSensorToOnFrame,     // -> handed to WebRTC via OnFrame(), after any scale/transform
    kSensorToEncodeIn,    // -> VideoEncoder::Encode() entry, i.e. after the encoder queue wait
    kSensorToEncoded,     // -> the encoded frame comes back out of the encoder
    kSensorToSent,        // -> OnEncodedImage() returned, i.e. packetized and handed to the pacer

    kCaptureCallback, // libcamera RequestComplete entry -> queueRequest (buffer starvation window)
    kArgusCopy,       // IImageNativeBuffer::copyToNvBuffer
    kI420Scale,       // ToI420() + I420Buffer::ScaleFrom
    kNvTransform,     // NvBufSurf::NvTransform
    kScalerDwell,     // scaler queue push -> pop on the worker thread
    kHwDecodeDwell,   // buffer queued to the hw decoder -> dequeued from the capture plane
    kHwEncodeDwell,   // buffer queued to the hw encoder -> dequeued from the capture plane
    kEncodeCall,      // duration of VideoEncoder::Encode() (the whole cost for sync encoders)
    kOnEncodedImage,  // duration of the downstream OnEncodedImage(): packetize + pacer handoff
    kCaptureInterval, // sensor timestamp delta between consecutive frames

    kStageCount,
};

enum class Counter : int {
    kFramesCaptured = 0,
    kFramesEncoded,
    kAdaptDrop,         // AdaptFrame() returned false
    kEncoderQueueDrop,  // handed to OnFrame() but never reached Encode()
    kScalerNoBuffer,    // scaler had no free buffer
    kScalerQueueFull,   // scaler task queue rejected the push
    kV4L2NoBuffer,      // v4l2 codec had no free output buffer
    kDecoderNoBuffer,   // decoder had no free frame buffer
    kDecoderDqTimeout,  // hw decoder dqBuffer() timed out
    kEncoderDqTimeout,  // hw encoder dqBuffer() timed out
    kRecorderQueueFull, // recorder queue was full, the frame never reached the recording encoder

    kCounterCount,
};

namespace internal {
extern std::atomic<bool> g_enabled;
} // namespace internal

// Hot-path guard. Every hook must call this first.
inline bool Enabled() { return internal::g_enabled.load(std::memory_order_relaxed); }

void Start(int interval_sec);
void Stop();

// CLOCK_MONOTONIC microseconds, the same clock webrtc::TimeMicros() uses.
int64_t NowUs();

// Converts a capturer's hardware timestamp into the NowUs() domain. The first call calibrates
// against CLOCK_BOOTTIME, since libcamera reports CLOCK_BOOTTIME on some kernels.
int64_t SensorUs(timeval tv);

void Record(Stage stage, int64_t duration_us);
inline void RecordSince(Stage stage, int64_t start_us) { Record(stage, NowUs() - start_us); }

// Records kSensorToCapture, kCaptureInterval and the captured-frame count in one call, so a
// capturer needs no state of its own to report the frame interval.
void RecordCapture(int64_t sensor_us, int64_t now_us);

void Count(Counter counter, uint64_t n = 1);

// Reported side by side so an adaptive downscale becomes visible without reaching for GetStats():
// under MAINTAIN_FRAMERATE, a CPU-limited encoder silently shrinks what is sent.
void SetSourceResolution(int width, int height);
void SetSentResolution(int width, int height);

// The sender's bitrate loop, reported next to the stage timings so a collapse shows up in the same
// place as the frame timings: what the congestion controller allocated, what the encoder was
// configured with after the adjuster's correction, and what it actually produced. A gauge, not a
// histogram -- with several peer connections the last encoder to update rates wins, the same way
// the resolution gauges behave.
void SetBitrateKbps(int allocated, int configured, int produced);

// What the capture side stashed for one delivered frame. `seq` counts the frames handed to
// OnFrame(), so the gap between two consecutive Encode() calls is exactly what
// VideoStreamEncoder threw away in between.
struct CaptureInfo {
    int64_t sensor_us = 0; // 0 when the frame is unknown
    uint32_t seq = 0;      // 0 when the frame is unknown
};

// Carries the sensor timestamp across stages that replace the frame buffer (ScaleTrackSource
// builds a fresh I420Buffer, dropping V4L2FrameBuffer::timestamp()). Keyed on the timestamp_us the
// track source assigns, which VideoStreamEncoder passes through to Encode() unchanged. Call it for
// the frames that are actually delivered, so that a frame AdaptFrame() rejected does not read as a
// missing one later.
void MarkCapture(int64_t frame_timestamp_us, int64_t sensor_us);
CaptureInfo LookupCapture(int64_t frame_timestamp_us);

// Same idea across the encoder, where the only key shared by the input frame and the resulting
// EncodedImage is the RTP timestamp. The hardware encoders complete on their own dequeue thread,
// so the input frame is long out of scope by then.
void MarkEncode(uint32_t rtp_timestamp, int64_t sensor_us);
int64_t LookupEncode(uint32_t rtp_timestamp); // 0 when the frame is unknown

} // namespace latency

#endif // COMMON_LATENCY_TRACER_H_
