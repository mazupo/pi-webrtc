#include "capturer/alsa_capturer.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "common/logging.h"

constexpr unsigned int kTargetLatencyUs = 40000;

std::shared_ptr<AudioCapturer> AlsaCapturer::Create(Args args) {
    auto ptr = std::make_shared<AlsaCapturer>(args);
    if (!ptr->CreateFloat32Source()) {
        return nullptr;
    }
    ptr->StartCapture();
    return ptr;
}

AlsaCapturer::AlsaCapturer(Args args)
    : AudioCapturer(args.channels, args.sample_rate),
      pcm_handle_(nullptr) {}

AlsaCapturer::~AlsaCapturer() {
    worker_.reset();
    if (pcm_handle_) {
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

bool AlsaCapturer::CreateFloat32Source() {
    const auto try_set_params = [this](snd_pcm_format_t alsa_fmt, SampleFormat fmt) -> bool {
        const int ret = snd_pcm_set_params(pcm_handle_, alsa_fmt, SND_PCM_ACCESS_RW_INTERLEAVED,
                                           channels_, sample_rate_, 1, kTargetLatencyUs);
        if (ret < 0) {
            return false;
        }
        sample_format_ = fmt;
        return true;
    };

    int ret = snd_pcm_open(&pcm_handle_, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (ret < 0) {
        ERROR_PRINT("ALSA open failed: %s", snd_strerror(ret));
        pcm_handle_ = nullptr;
        return false;
    }

    if (try_set_params(SND_PCM_FORMAT_FLOAT_LE, SampleFormat::Float32)) {
        INFO_PRINT("ALSA capture format: FLOAT_LE");
    } else if (try_set_params(SND_PCM_FORMAT_S32_LE, SampleFormat::S32)) {
        INFO_PRINT("ALSA capture format fallback: S32_LE");
    } else if (try_set_params(SND_PCM_FORMAT_S16_LE, SampleFormat::S16)) {
        INFO_PRINT("ALSA capture format fallback: S16_LE");
    } else {
        ERROR_PRINT("ALSA set params failed for FLOAT_LE/S32_LE/S16_LE");
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
        return false;
    }

    // Pre-allocate buffers for exactly 10ms of audio (same fragment size as PaCapturer).
    const size_t frames_per_buffer = static_cast<size_t>(frames_per_chunk());
    raw_capture_buffer_.resize(frames_per_buffer * channels_ * sizeof(float));
    float_capture_buffer_.resize(frames_per_buffer * channels_);
    return true;
}

void AlsaCapturer::CaptureSamples() {
    if (!pcm_handle_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return;
    }

    const size_t frames_per_buffer = static_cast<size_t>(frames_per_chunk()); // 10ms

    const auto frames_read =
        snd_pcm_readi(pcm_handle_, raw_capture_buffer_.data(), frames_per_buffer);
    if (frames_read == -EAGAIN) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return;
    }
    if (frames_read == -EPIPE) {
        snd_pcm_prepare(pcm_handle_);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return;
    }
    if (frames_read < 0) {
        ERROR_PRINT("ALSA read failed: %s", snd_strerror(static_cast<int>(frames_read)));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return;
    }

    const size_t sample_count = static_cast<size_t>(frames_read) * channels_;

    if (sample_format_ == SampleFormat::Float32) {
        std::memcpy(float_capture_buffer_.data(), raw_capture_buffer_.data(),
                    sample_count * sizeof(float));
    } else if (sample_format_ == SampleFormat::S32) {
        constexpr float scale = 1.0f / 2147483648.0f;
        for (size_t i = 0; i < sample_count; ++i) {
            int32_t val;
            std::memcpy(&val, raw_capture_buffer_.data() + i * sizeof(int32_t), sizeof(int32_t));
            float_capture_buffer_[i] = static_cast<float>(val) * scale;
        }
    } else {
        constexpr float scale = 1.0f / 32768.0f;
        for (size_t i = 0; i < sample_count; ++i) {
            int16_t val;
            std::memcpy(&val, raw_capture_buffer_.data() + i * sizeof(int16_t), sizeof(int16_t));
            float_capture_buffer_[i] = static_cast<float>(val) * scale;
        }
    }

    shared_buffer_ = {.start = reinterpret_cast<uint8_t *>(float_capture_buffer_.data()),
                      .length = static_cast<unsigned int>(sample_count),
                      .channels = static_cast<unsigned int>(channels_)};
    Next(shared_buffer_);
}

void AlsaCapturer::StartCapture() {
    worker_ = std::make_unique<Worker>("AlsaCapture", [this]() {
        CaptureSamples();
    });
    worker_->Run();
}
