#ifndef RECORDER_MANAGER_H_
#define RECORDER_MANAGER_H_

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

#include "capturer/audio_capturer.h"
#include "capturer/frame_source.h"
#include "recorder/audio_recorder.h"
#include "recorder/recorder_config.h"
#include "recorder/video_recorder.h"

class RecorderManager {
  public:
    static std::unique_ptr<RecorderManager> Create(std::shared_ptr<FrameSource> video_src,
                                                   std::shared_ptr<AudioCapturer> audio_src,
                                                   RecorderConfig config, bool auto_start = true);
    RecorderManager(RecorderConfig config);
    ~RecorderManager();
    void WriteIntoFile(AVPacket *pkt);
    void Start();
    void Stop();
    bool is_recording() const;
    std::string current_filepath() const;

  protected:
    std::mutex ctx_mux;
    RecorderConfig config;
    uint fps;
    int width;
    int height;
    std::string record_path;
    AVFormatContext *fmt_ctx;
    std::atomic<bool> is_recording_;
    std::unique_ptr<VideoRecorder> video_recorder;
    std::unique_ptr<AudioRecorder> audio_recorder;

    void CreateVideoRecorder(std::shared_ptr<FrameSource> video_src);
    void CloseCurrentFile();
    void CreateAudioRecorder(std::shared_ptr<AudioCapturer> audio_src);
    void SubscribeVideoSource(std::shared_ptr<FrameSource> video_src);
    void SubscribeAudioSource(std::shared_ptr<AudioCapturer> audio_src);

  private:
    bool auto_start_;
    int file_number_ = 0;
    std::atomic<bool> header_written_;
    std::mutex rotation_mtx_;
    std::condition_variable rotation_cv_;
    std::atomic<bool> rotation_abort_;
    std::atomic<bool> rotation_requested_;
    std::thread rotation_thread_;
    struct timeval base_start_time_;
    std::shared_ptr<FrameSource> video_src_;

    std::string current_filepath_;

    Subscription audio_subscription_;
    Subscription video_subscription_;

    void StartRotationThread();
    void MakePreviewImage(std::string path);
    std::string ReplaceExtension(const std::string &url, const std::string &new_extension);
};

#endif // RECORDER_MANAGER_H_
