#ifndef AUDIO_DEVICE_BRIDGE_H_
#define AUDIO_DEVICE_BRIDGE_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <api/audio/audio_device.h>
#include <api/make_ref_counted.h>
#include <api/scoped_refptr.h>

#include "capturer/audio_capturer.h"
#include "common/interface/subject.h"
#include "common/logging.h"
#include "common/worker.h"
#include "player/alsa_player.h"
#include "player/audio_player.h"
#include "player/pa_player.h"

// Own playout thread instead of WebRTC's Linux ADMs, which might deadlock or
// stall the worker thread on reconnect in M146.
class AudioDeviceBridge : public webrtc::AudioDeviceModule {
  public:
    static webrtc::scoped_refptr<AudioDeviceBridge> Create(AudioLayer audio_layer) {
        std::unique_ptr<AudioPlayer> player;
        if (audio_layer == kLinuxPulseAudio) {
            player = PaPlayer::Create(kPlayoutSampleRate, kPlayoutChannels);
        } else if (audio_layer == kLinuxAlsaAudio) {
            player = AlsaPlayer::Create(kPlayoutSampleRate, kPlayoutChannels);
        }
        return webrtc::make_ref_counted<AudioDeviceBridge>(std::move(player));
    }

    explicit AudioDeviceBridge(std::unique_ptr<AudioPlayer> player)
        : player_(std::move(player)) {
        if (player_) {
            play_buf_.resize(static_cast<size_t>(player_->sample_rate() / 100) *
                             player_->channels());
        }
    }
    ~AudioDeviceBridge() override { StopPlayout(); }

    void SetCapturer(std::shared_ptr<AudioCapturer> capturer) {
        if (capturer) {
            sample_rate_ = capturer->sample_rate();
            // Pre-allocate the int16 conversion buffer to match AudioCapturer's
            // exact 10ms output size: frames_per_chunk() frames * channels.
            pcm_buf_.resize(static_cast<size_t>(capturer->frames_per_chunk()) *
                            capturer->channels());
            subscription_ = capturer->Subscribe([this](const AudioBuffer &buf) {
                OnAudioBuffer(buf);
            });
        } else {
            subscription_ = {};
        }
    }

    // ---- Core methods ----

    // Always succeeds; atomically updates the transport pointer so no guard on
    // recording_ is needed. Safe to call while the capture thread is running.
    int32_t RegisterAudioCallback(webrtc::AudioTransport *transport) override {
        audio_transport_.store(transport, std::memory_order_release);
        return 0;
    }

    int32_t Init() override {
        initialized_.store(true);
        return 0;
    }

    int32_t Terminate() override {
        StopPlayout();
        subscription_ = {};
        audio_transport_.store(nullptr, std::memory_order_release);
        initialized_.store(false);
        return 0;
    }

    bool Initialized() const override { return initialized_.load(); }

    // Instant no-ops — no underlying audio stream to start/stop here.
    int32_t StartRecording() override {
        recording_.store(true);
        return 0;
    }
    int32_t StopRecording() override {
        recording_.store(false);
        return 0;
    }
    bool Recording() const override { return recording_.load(); }

    // ---- Playout ----

    // WebRTC reads *available unconditionally.
    int32_t PlayoutIsAvailable(bool *available) override {
        *available = player_ != nullptr;
        return 0;
    }
    int32_t InitPlayout() override {
        if (!player_ || playout_initialized_) {
            return 0;
        }
        if (!player_->Open()) {
            ERROR_PRINT("Failed to open audio playout device; remote audio is muted.");
            return -1;
        }
        playout_initialized_ = true;
        return 0;
    }
    bool PlayoutIsInitialized() const override { return playout_initialized_; }

    int32_t StartPlayout() override {
        if (!player_ || playout_worker_) {
            return 0;
        }
        if (InitPlayout() != 0) {
            return -1;
        }
        playout_delay_ms_.store(0);
        playout_worker_ = std::make_unique<Worker>("AudioPlayout", [this]() {
            PlayoutOnce();
        });
        playout_worker_->Run();
        playing_.store(true);
        return 0;
    }
    int32_t StopPlayout() override {
        playing_.store(false);
        playout_worker_.reset();
        if (player_ && playout_initialized_) {
            player_->Close();
        }
        playout_initialized_ = false;
        return 0;
    }
    bool Playing() const override { return playing_.load(); }

    int16_t PlayoutDevices() override { return player_ ? 1 : 0; }
    int32_t PlayoutDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
                              char guid[webrtc::kAdmMaxGuidSize]) override {
        return 0;
    }
    int32_t SetPlayoutDevice(uint16_t index) override { return 0; }
    int32_t SetPlayoutDevice(WindowsDeviceType device) override { return 0; }
    int32_t InitSpeaker() override { return 0; }
    bool SpeakerIsInitialized() const override { return player_ != nullptr; }

    int32_t SpeakerVolumeIsAvailable(bool *available) override {
        *available = false;
        return 0;
    }
    int32_t SetSpeakerVolume(uint32_t volume) override { return 0; }
    int32_t SpeakerVolume(uint32_t *volume) const override { return 0; }
    int32_t MaxSpeakerVolume(uint32_t *maxVolume) const override { return 0; }
    int32_t MinSpeakerVolume(uint32_t *minVolume) const override { return 0; }

    int32_t SpeakerMuteIsAvailable(bool *available) override {
        *available = false;
        return 0;
    }
    int32_t SetSpeakerMute(bool enable) override { return 0; }
    int32_t SpeakerMute(bool *enabled) const override { return 0; }
    int32_t StereoPlayoutIsAvailable(bool *available) const override {
        *available = player_ && player_->channels() == 2;
        return 0;
    }
    int32_t SetStereoPlayout(bool enable) override { return 0; }
    int32_t StereoPlayout(bool *enabled) const override {
        *enabled = player_ && player_->channels() == 2;
        return 0;
    }
    // PlayoutDelay is called unconditionally for stats; must set *delayMS.
    int32_t PlayoutDelay(uint16_t *delayMS) const override {
        *delayMS = static_cast<uint16_t>(playout_delay_ms_.load(std::memory_order_relaxed));
        return 0;
    }

    // ---- Recording ----

    int32_t ActiveAudioLayer(AudioLayer *audioLayer) const override {
        *audioLayer = kPlatformDefaultAudio;
        return 0;
    }
    int16_t RecordingDevices() override { return 1; }
    int32_t RecordingDeviceName(uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
                                char guid[webrtc::kAdmMaxGuidSize]) override {
        return 0;
    }
    int32_t SetRecordingDevice(uint16_t index) override { return 0; }
    int32_t SetRecordingDevice(WindowsDeviceType device) override { return 0; }
    // RecordingIsAvailable: WebRTC reads *available unconditionally.
    int32_t RecordingIsAvailable(bool *available) override {
        *available = true;
        return 0;
    }
    int32_t InitRecording() override { return 0; }
    bool RecordingIsInitialized() const override { return true; }
    int32_t InitMicrophone() override { return 0; }
    bool MicrophoneIsInitialized() const override { return true; }
    // MicrophoneVolumeIsAvailable = false → WebRTC never calls the volume getters/setters below.
    int32_t MicrophoneVolumeIsAvailable(bool *available) override {
        *available = false;
        return 0;
    }
    int32_t SetMicrophoneVolume(uint32_t volume) override { return 0; }
    int32_t MicrophoneVolume(uint32_t *volume) const override { return 0; }
    int32_t MaxMicrophoneVolume(uint32_t *maxVolume) const override { return 0; }
    int32_t MinMicrophoneVolume(uint32_t *minVolume) const override { return 0; }
    // MicrophoneMuteIsAvailable = false → WebRTC never calls the mute getters/setters below.
    int32_t MicrophoneMuteIsAvailable(bool *available) override {
        *available = false;
        return 0;
    }
    int32_t SetMicrophoneMute(bool enable) override { return 0; }
    int32_t MicrophoneMute(bool *enabled) const override { return 0; }
    // StereoRecordingIsAvailable = true → StereoRecording getter IS called; must set *enabled.
    int32_t StereoRecordingIsAvailable(bool *available) const override {
        *available = true;
        return 0;
    }
    int32_t SetStereoRecording(bool enable) override { return 0; }
    int32_t StereoRecording(bool *enabled) const override {
        *enabled = true;
        return 0;
    }
    bool BuiltInAECIsAvailable() const override { return false; }
    bool BuiltInAGCIsAvailable() const override { return false; }
    bool BuiltInNSIsAvailable() const override { return false; }
    int32_t EnableBuiltInAEC(bool enable) override { return 0; }
    int32_t EnableBuiltInAGC(bool enable) override { return 0; }
    int32_t EnableBuiltInNS(bool enable) override { return 0; }

  private:
    static constexpr int kPlayoutSampleRate = 48000;
    static constexpr int kPlayoutChannels = 2;

    // 10ms per call, paced by the blocking player write.
    void PlayoutOnce() {
        const size_t channels = static_cast<size_t>(player_->channels());
        const size_t frames = play_buf_.size() / channels;

        bool has_data = false;
        auto *transport = audio_transport_.load(std::memory_order_acquire);
        if (transport) {
            size_t samples_out = 0;
            int64_t elapsed_time_ms = -1;
            int64_t ntp_time_ms = -1;
            has_data = transport->NeedMorePlayData(frames, sizeof(int16_t) * channels, channels,
                                                   static_cast<uint32_t>(player_->sample_rate()),
                                                   play_buf_.data(), samples_out, &elapsed_time_ms,
                                                   &ntp_time_ms) == 0;
        }
        if (!has_data) {
            std::fill(play_buf_.begin(), play_buf_.end(), 0);
        }

        if (!player_->Write(play_buf_.data(), frames)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(AudioCapturer::kChunkDurationMs));
            return;
        }
        playout_delay_ms_.store(player_->DelayMs(), std::memory_order_relaxed);
    }

    void OnAudioBuffer(const AudioBuffer &buf) {
        if (!initialized_.load(std::memory_order_acquire) ||
            !recording_.load(std::memory_order_acquire))
            return;

        auto *transport = audio_transport_.load(std::memory_order_acquire);
        if (!transport)
            return;

        if (!buf.start || buf.channels == 0 || buf.length == 0)
            return;

        const float *src = reinterpret_cast<const float *>(buf.start);
        const size_t n_samples = buf.length; // interleaved float32 values
        const size_t n_channels = buf.channels;
        if (n_samples % n_channels != 0)
            return;

        const size_t n_frames = n_samples / n_channels;

        if (pcm_buf_.size() < n_samples) {
            pcm_buf_.resize(n_samples);
        }

        // Convert float32 [-1, 1] → int16 for WebRTC's 16-bit PCM pipeline.
        for (size_t i = 0; i < n_samples; ++i) {
            float f = src[i];
            f = std::max(-1.0f, std::min(1.0f, f));
            pcm_buf_[i] = static_cast<int16_t>(f * 32767.0f);
        }

        // nBytesPerSample is bytes per interleaved frame.
        uint32_t new_mic_level = 100;
        transport->RecordedDataIsAvailable(pcm_buf_.data(), n_frames, sizeof(int16_t) * n_channels,
                                           n_channels, static_cast<uint32_t>(sample_rate_),
                                           /*totalDelayMS=*/0, /*clockDrift=*/0,
                                           /*currentMicLevel=*/100, /*keyPressed=*/false,
                                           new_mic_level, -1);
    }

    std::atomic<webrtc::AudioTransport *> audio_transport_{nullptr};
    std::atomic<bool> recording_{false};
    std::atomic<bool> initialized_{false};
    int sample_rate_ = 48000;
    std::vector<int16_t> pcm_buf_;
    Subscription subscription_;

    std::unique_ptr<AudioPlayer> player_;
    std::vector<int16_t> play_buf_;
    std::atomic<bool> playing_{false};
    std::atomic<int> playout_delay_ms_{0};
    bool playout_initialized_ = false;
    std::unique_ptr<Worker> playout_worker_;
};

#endif // AUDIO_DEVICE_BRIDGE_H_
