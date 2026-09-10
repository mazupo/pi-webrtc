#include "common/latency_tracer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>

#include "common/logging.h"
#include "common/worker.h"

namespace latency {
namespace internal {
std::atomic<bool> g_enabled{false};
} // namespace internal

namespace {

constexpr int kBucketCount = 256;
constexpr int64_t kBucketWidthUs = 500; // 0.5 ms per bucket, so 0..128 ms is resolved

// Side table depth. An entry only has to survive the handful of frames between the mark and the
// lookup, but timestamp jitter makes the hash revisit a slot early now and then, and every such
// eviction costs a sample and shows up as one phantom encoder_queue_drop. 1024 slots keep that
// under a few tenths of a percent for 24 kB a table.
constexpr int kSlotBits = 10;
constexpr int kSlotCount = 1 << kSlotBits;

const char *const kStageNames[] = {
    "sensor->capture", "sensor->track_in", "sensor->onframe",  "sensor->encode_in",
    "sensor->encoded", "sensor->sent",     "capture_cb_work",  "argus_copy",
    "i420_scale",      "nvtransform",      "scaler_dwell",     "hw_decode_dwell",
    "hw_encode_dwell", "encode_call",      "on_encoded_image", "capture_interval",
};
static_assert(sizeof(kStageNames) / sizeof(kStageNames[0]) ==
                  static_cast<size_t>(Stage::kStageCount),
              "kStageNames must stay in sync with Stage");

const char *const kCounterNames[] = {
    "captured",     "encoded",    "adapt_drop",    "encoder_queue_drop", "scaler_nobuf",
    "scaler_qfull", "v4l2_nobuf", "decoder_nobuf", "decoder_dq_timeout", "dq_timeout",
};
static_assert(sizeof(kCounterNames) / sizeof(kCounterNames[0]) ==
                  static_cast<size_t>(Counter::kCounterCount),
              "kCounterNames must stay in sync with Counter");

struct Histogram {
    std::atomic<uint32_t> buckets[kBucketCount];
    std::atomic<int64_t> max_us;
    std::atomic<int64_t> min_us;
};

struct Snapshot {
    uint64_t count = 0;
    int64_t p50_us = 0;
    int64_t p95_us = 0;
    int64_t min_us = 0;
    int64_t max_us = 0;
};

size_t SlotOf(int64_t key) {
    // Multiplicative hash so the slot stride does not depend on the frame interval. The top
    // kSlotBits of the product are the best mixed, and taking exactly that many needs no mask.
    const uint64_t h = static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull;
    return static_cast<size_t>(h >> (64 - kSlotBits));
}

// A fixed ring that carries the sensor timestamp across a stage that cannot pass it along in the
// frame itself. Single writer, single reader; a key that no longer matches simply means the frame
// was evicted and the sample is skipped.
struct SideTable {
    struct Slot {
        std::atomic<int64_t> key;
        std::atomic<int64_t> sensor_us;
        std::atomic<uint32_t> seq;
    };

    Slot slots[kSlotCount];

    void Mark(int64_t key, int64_t sensor_us, uint32_t seq) {
        Slot &slot = slots[SlotOf(key)];
        // Publish the values before the key so a reader that sees the key sees matching values.
        slot.sensor_us.store(sensor_us, std::memory_order_relaxed);
        slot.seq.store(seq, std::memory_order_relaxed);
        slot.key.store(key, std::memory_order_release);
    }

    CaptureInfo Lookup(int64_t key) const {
        const Slot &slot = slots[SlotOf(key)];
        if (slot.key.load(std::memory_order_acquire) != key) {
            return {};
        }
        return {slot.sensor_us.load(std::memory_order_relaxed),
                slot.seq.load(std::memory_order_relaxed)};
    }
};

Histogram g_histograms[static_cast<int>(Stage::kStageCount)];
std::atomic<uint64_t> g_counters[static_cast<int>(Counter::kCounterCount)];
SideTable g_capture_table; // keyed on VideoFrame::timestamp_us()
SideTable g_encode_table;  // keyed on the RTP timestamp

std::atomic<int64_t> g_last_sensor_us{0};
std::atomic<uint32_t> g_capture_seq{0};

std::atomic<int> g_src_width{0};
std::atomic<int> g_src_height{0};
std::atomic<int> g_adapted_width{0};
std::atomic<int> g_adapted_height{0};

std::atomic<int> g_allocated_kbps{0};
std::atomic<int> g_configured_kbps{0};
std::atomic<int> g_produced_kbps{0};

// Offset added to a capturer timestamp to move it into the NowUs() domain. Zero once the
// capturer is confirmed to report CLOCK_MONOTONIC, which is the common case.
std::atomic<int64_t> g_clock_offset_us{0};
std::atomic<bool> g_clock_calibrated{false};

std::unique_ptr<Worker> g_reporter;
std::mutex g_reporter_mutex;
std::condition_variable g_reporter_cv;
bool g_stopped = false;
int g_interval_sec = 5;
int64_t g_last_report_us = 0;

int64_t BootUs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

// Decides once whether the capturer's clock matches NowUs(). A libcamera buffer timestamp is
// CLOCK_BOOTTIME on some kernels, which on a machine that has suspended reads seconds ahead of
// CLOCK_MONOTONIC and would make every stage look negative.
void CalibrateClock(int64_t sensor_us) {
    const int64_t mono_now = NowUs();
    const int64_t boot_now = BootUs();

    // A sane sensor timestamp is a few tens of milliseconds behind now, never ahead of it and
    // never more than a second behind.
    const int64_t mono_delta = mono_now - sensor_us;
    if (mono_delta > -1000 && mono_delta < 1000000) {
        g_clock_offset_us.store(0, std::memory_order_relaxed);
    } else {
        const int64_t boot_delta = boot_now - sensor_us;
        if (boot_delta > -1000 && boot_delta < 1000000) {
            // The capturer is on CLOCK_BOOTTIME; shift its stamps into CLOCK_MONOTONIC.
            g_clock_offset_us.store(mono_now - boot_now, std::memory_order_relaxed);
            INFO_PRINT("latency: capturer clock is CLOCK_BOOTTIME, applying %.1f ms offset.",
                       (mono_now - boot_now) / 1000.0);
        } else {
            g_clock_offset_us.store(0, std::memory_order_relaxed);
            ERROR_PRINT("latency: capturer timestamp is in an unknown clock domain "
                        "(monotonic delta %.1f ms, boottime delta %.1f ms). "
                        "sensor->* stages will be meaningless.",
                        mono_delta / 1000.0, boot_delta / 1000.0);
        }
    }

    g_clock_calibrated.store(true, std::memory_order_release);
}

Snapshot TakeSnapshot(Histogram &hist) {
    Snapshot snap;
    uint32_t counts[kBucketCount];
    uint64_t total = 0;

    for (int i = 0; i < kBucketCount; i++) {
        counts[i] = hist.buckets[i].exchange(0, std::memory_order_relaxed);
        total += counts[i];
    }

    snap.count = total;
    snap.max_us = hist.max_us.exchange(0, std::memory_order_relaxed);
    snap.min_us = hist.min_us.exchange(INT64_MAX, std::memory_order_relaxed);
    if (snap.min_us == INT64_MAX) {
        snap.min_us = 0;
    }

    if (total == 0) {
        return snap;
    }

    // Report the upper edge of the bucket, so a value never reads as faster than it was.
    const uint64_t p50_target = total / 2;
    const uint64_t p95_target = (total * 95) / 100;
    uint64_t cumulative = 0;
    bool p50_done = false;
    for (int i = 0; i < kBucketCount; i++) {
        cumulative += counts[i];
        if (!p50_done && cumulative > p50_target) {
            snap.p50_us = static_cast<int64_t>(i + 1) * kBucketWidthUs;
            p50_done = true;
        }
        if (cumulative > p95_target) {
            snap.p95_us = static_cast<int64_t>(i + 1) * kBucketWidthUs;
            break;
        }
    }

    // Percentiles are reported at the bucket's upper edge, which can land above the largest
    // sample when everything fits in one bucket. Clamping keeps p50 <= p95 <= max.
    snap.p95_us = std::min(snap.p95_us, snap.max_us);
    snap.p50_us = std::min(snap.p50_us, snap.p95_us);

    return snap;
}

void Report() {
    const int64_t now = NowUs();
    const double elapsed_sec = (now - g_last_report_us) / 1000000.0;
    g_last_report_us = now;

    Snapshot snaps[static_cast<int>(Stage::kStageCount)];
    for (int i = 0; i < static_cast<int>(Stage::kStageCount); i++) {
        snaps[i] = TakeSnapshot(g_histograms[i]);
    }

    uint64_t counters[static_cast<int>(Counter::kCounterCount)];
    for (int i = 0; i < static_cast<int>(Counter::kCounterCount); i++) {
        counters[i] = g_counters[i].exchange(0, std::memory_order_relaxed);
    }

    const uint64_t captured = counters[static_cast<int>(Counter::kFramesCaptured)];
    const uint64_t encoded = counters[static_cast<int>(Counter::kFramesEncoded)];

    std::string table;
    table.reserve(2048);
    char line[256];

    std::snprintf(line, sizeof(line), "\n  %-18s %8s %8s %8s %8s %8s\n", "stage", "p50", "p95",
                  "min", "max", "n");
    table += line;

    bool delta_header_printed = false;
    for (int i = 0; i < static_cast<int>(Stage::kStageCount); i++) {
        const Snapshot &s = snaps[i];
        if (s.count == 0) {
            continue; // stage not on this pipeline
        }
        if (i >= static_cast<int>(Stage::kCaptureCallback) && !delta_header_printed) {
            table += "  -- per-stage cost --\n";
            delta_header_printed = true;
        }
        std::snprintf(line, sizeof(line), "  %-18s %8.2f %8.2f %8.2f %8.2f %8llu\n", kStageNames[i],
                      s.p50_us / 1000.0, s.p95_us / 1000.0, s.min_us / 1000.0, s.max_us / 1000.0,
                      static_cast<unsigned long long>(s.count));
        table += line;
    }

    table += "  -- drops --";
    for (int i = static_cast<int>(Counter::kAdaptDrop);
         i < static_cast<int>(Counter::kCounterCount); i++) {
        std::snprintf(line, sizeof(line), " %s=%llu", kCounterNames[i],
                      static_cast<unsigned long long>(counters[i]));
        table += line;
    }

    std::snprintf(line, sizeof(line),
                  "\n  -- bitrate kbps -- allocated %d  configured %d  produced %d",
                  g_allocated_kbps.load(std::memory_order_relaxed),
                  g_configured_kbps.load(std::memory_order_relaxed),
                  g_produced_kbps.load(std::memory_order_relaxed));
    table += line;

    std::snprintf(line, sizeof(line), "\n  -- resolution -- src %dx%d  sent %dx%d",
                  g_src_width.load(std::memory_order_relaxed),
                  g_src_height.load(std::memory_order_relaxed),
                  g_adapted_width.load(std::memory_order_relaxed),
                  g_adapted_height.load(std::memory_order_relaxed));
    table += line;

    INFO_PRINT("==== latency over %.1fs: captured %.1f fps, encoded %.1f fps ====%s", elapsed_sec,
               elapsed_sec > 0 ? captured / elapsed_sec : 0.0,
               elapsed_sec > 0 ? encoded / elapsed_sec : 0.0, table.c_str());

    // stdout is block buffered when redirected to a file, which is how these runs are usually
    // captured. Once per interval this costs nothing.
    fflush(stdout);
}

} // namespace

int64_t NowUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

int64_t SensorUs(timeval tv) {
    const int64_t raw = static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
    if (raw == 0) {
        return 0; // the capturer never stamped this buffer
    }

    if (!g_clock_calibrated.load(std::memory_order_acquire)) {
        CalibrateClock(raw);
    }

    return raw + g_clock_offset_us.load(std::memory_order_relaxed);
}

void Record(Stage stage, int64_t duration_us) {
    if (duration_us < 0) {
        duration_us = 0;
    }

    Histogram &hist = g_histograms[static_cast<int>(stage)];

    int64_t bucket = duration_us / kBucketWidthUs;
    if (bucket >= kBucketCount) {
        bucket = kBucketCount - 1;
    }
    hist.buckets[bucket].fetch_add(1, std::memory_order_relaxed);

    int64_t prev_max = hist.max_us.load(std::memory_order_relaxed);
    while (duration_us > prev_max &&
           !hist.max_us.compare_exchange_weak(prev_max, duration_us, std::memory_order_relaxed)) {
    }

    int64_t prev_min = hist.min_us.load(std::memory_order_relaxed);
    while (duration_us < prev_min &&
           !hist.min_us.compare_exchange_weak(prev_min, duration_us, std::memory_order_relaxed)) {
    }
}

void RecordCapture(int64_t sensor_us, int64_t now_us) {
    Count(Counter::kFramesCaptured);

    if (sensor_us == 0) {
        return; // the capturer never stamped this buffer
    }

    Record(Stage::kSensorToCapture, now_us - sensor_us);

    const int64_t previous = g_last_sensor_us.exchange(sensor_us, std::memory_order_relaxed);
    if (previous != 0) {
        Record(Stage::kCaptureInterval, sensor_us - previous);
    }
}

void Count(Counter counter, uint64_t n) {
    g_counters[static_cast<int>(counter)].fetch_add(n, std::memory_order_relaxed);
}

void SetSourceResolution(int width, int height) {
    g_src_width.store(width, std::memory_order_relaxed);
    g_src_height.store(height, std::memory_order_relaxed);
}

void SetSentResolution(int width, int height) {
    g_adapted_width.store(width, std::memory_order_relaxed);
    g_adapted_height.store(height, std::memory_order_relaxed);
}

void SetBitrateKbps(int allocated, int configured, int produced) {
    g_allocated_kbps.store(allocated, std::memory_order_relaxed);
    g_configured_kbps.store(configured, std::memory_order_relaxed);
    g_produced_kbps.store(produced, std::memory_order_relaxed);
}

void MarkCapture(int64_t frame_timestamp_us, int64_t sensor_us) {
    // Sequence numbers start at 1, so that 0 keeps meaning "this frame is unknown".
    const uint32_t seq = g_capture_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    g_capture_table.Mark(frame_timestamp_us, sensor_us, seq);
}

CaptureInfo LookupCapture(int64_t frame_timestamp_us) {
    return g_capture_table.Lookup(frame_timestamp_us);
}

void MarkEncode(uint32_t rtp_timestamp, int64_t sensor_us) {
    g_encode_table.Mark(rtp_timestamp, sensor_us, 0);
}

int64_t LookupEncode(uint32_t rtp_timestamp) {
    return g_encode_table.Lookup(rtp_timestamp).sensor_us;
}

void Start(int interval_sec) {
    if (g_reporter) {
        return;
    }

    g_interval_sec = interval_sec > 0 ? interval_sec : 5;
    g_last_report_us = NowUs();

    for (int i = 0; i < static_cast<int>(Stage::kStageCount); i++) {
        g_histograms[i].min_us.store(INT64_MAX, std::memory_order_relaxed);
    }

    {
        std::lock_guard<std::mutex> lock(g_reporter_mutex);
        g_stopped = false;
    }

    internal::g_enabled.store(true, std::memory_order_relaxed);

    g_reporter = std::make_unique<Worker>("latency reporter", []() {
        std::unique_lock<std::mutex> lock(g_reporter_mutex);

        g_reporter_cv.wait_for(lock, std::chrono::seconds(g_interval_sec), [] {
            return g_stopped;
        });

        if (g_stopped) {
            return;
        }

        lock.unlock();

        Report();
    });
    g_reporter->Run();

    INFO_PRINT("latency tracing is on, reporting every %d s.", g_interval_sec);
}

void Stop() {
    internal::g_enabled.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_reporter_mutex);
        g_stopped = true;
    }
    g_reporter_cv.notify_all();
    g_reporter.reset();
}

} // namespace latency
