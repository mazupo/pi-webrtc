#include "capturer/pa_capturer.h"

#include <chrono>
#include <thread>

#include "common/logging.h"

std::shared_ptr<AudioCapturer> PaCapturer::Create(Args args) {
    auto ptr = std::make_shared<PaCapturer>(args);
    if (!ptr->CreateFloat32Source()) {
        return nullptr;
    }
    ptr->StartCapture();
    return ptr;
}

PaCapturer::PaCapturer(Args args)
    : AudioCapturer(args.channels, args.sample_rate),
      src(nullptr) {}

PaCapturer::~PaCapturer() {
    worker_.reset();
    if (src) {
        pa_simple_free(src);
    }
}

bool PaCapturer::CreateFloat32Source() {
    int error;
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = channels_;
    ss.rate = sample_rate_;

    // Set fragsize so the PulseAudio server delivers data in exact 10ms fragments.
    const uint32_t frag_bytes = static_cast<uint32_t>(
        static_cast<size_t>(frames_per_chunk()) * static_cast<size_t>(channels_) * sizeof(float));
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.fragsize = frag_bytes;
    attr.tlength = static_cast<uint32_t>(-1);
    attr.prebuf = static_cast<uint32_t>(-1);
    attr.minreq = static_cast<uint32_t>(-1);

    src = pa_simple_new(nullptr, "Microphone", PA_STREAM_RECORD, nullptr, "record", &ss, nullptr,
                        &attr, &error);
    if (!src) {
        ERROR_PRINT("%s", pa_strerror(error));
        return false;
    }

    // Pre-allocate the capture buffer for exactly 10ms of stereo float32 audio.
    const size_t n_samples =
        static_cast<size_t>(frames_per_chunk()) * static_cast<size_t>(channels_);
    capture_buf_.resize(n_samples);

    INFO_PRINT("PulseAudio capture format: FLOAT32LE, %d channels, %d Hz", channels_, sample_rate_);

    return true;
}

void PaCapturer::CaptureSamples() {
    if (!src) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return;
    }

    int error;
    // Read exactly 10ms of audio per call so all consumers (WebRTC, recorder)
    const size_t n_frames = static_cast<size_t>(frames_per_chunk());
    const size_t n_samples = n_frames * static_cast<size_t>(channels_); // interleaved float32 count

    if (pa_simple_read(src, capture_buf_.data(), capture_buf_.size() * sizeof(float), &error) < 0) {
        ERROR_PRINT("pa_simple_read() failed: %s", pa_strerror(error));
        pa_simple_free(src);
        src = nullptr;
        return;
    }

    shared_buffer_ = {.start = reinterpret_cast<uint8_t *>(capture_buf_.data()),
                      .length = static_cast<unsigned int>(n_samples),
                      .channels = static_cast<unsigned int>(channels_)};
    subject_.Next(shared_buffer_);
}

void PaCapturer::StartCapture() {
    worker_ = std::make_unique<Worker>("PaCapture", [this]() {
        CaptureSamples();
    });
    worker_->Run();
}
