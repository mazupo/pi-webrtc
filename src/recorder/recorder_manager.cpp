#include "recorder/recorder_manager.h"

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <future>
#include <mutex>
#include <sstream>
#include <sys/statvfs.h>
#include <thread>

#include "common/jpeg_util.h"
#include "common/logging.h"
#include "common/utils.h"
#include "common/v4l2_frame_buffer.h"
#include "recorder/openh264_recorder.h"
#include "recorder/raw_h264_recorder.h"
#if defined(USE_RPI_HW_ENCODER)
#include "recorder/v4l2_h264_recorder.h"
#elif defined(USE_JETSON_HW_ENCODER)
#include "recorder/jetson_recorder.h"
#endif

namespace fs = std::filesystem;

namespace {

std::string PrefixZero(int src, int digits) {
    std::string str = std::to_string(src);
    std::string n_zero(digits - str.length(), '0');
    return n_zero + str;
}

bool CheckDriveSpace(const std::string &file_path, unsigned long min_free_byte) {
    struct statvfs stat;
    if (statvfs(file_path.c_str(), &stat) != 0) {
        return false;
    }
    return (stat.f_bsize * stat.f_bavail) >= min_free_byte;
}

void RotateFiles(const std::string &folder_path) {
    fs::path oldest_date_folder;
    for (const auto &entry : fs::directory_iterator(folder_path)) {
        if (entry.is_directory() &&
            (oldest_date_folder.empty() || entry.path() < oldest_date_folder)) {
            oldest_date_folder = entry.path();
        }
    }

    if (oldest_date_folder.empty()) {
        return;
    }

    fs::path oldest_hour_folder;
    for (const auto &hour_entry : fs::directory_iterator(oldest_date_folder)) {
        if (hour_entry.is_directory() &&
            (oldest_hour_folder.empty() || hour_entry.path() < oldest_hour_folder)) {
            oldest_hour_folder = hour_entry.path();
        }
    }

    try {
        if (oldest_hour_folder.empty()) {
            fs::remove_all(oldest_date_folder);
            INFO_PRINT("Deleted empty date folder: %s", oldest_date_folder.string().c_str());
            return;
        }
        fs::path oldest_file;
        for (const auto &file : fs::directory_iterator(oldest_hour_folder)) {
            if (file.is_regular_file()) {
                const auto &ext = file.path().extension();
                if (ext == ".mp4" || ext == ".jpg") {
                    if (oldest_file.empty() || file.path().filename() < oldest_file.filename()) {
                        oldest_file = file.path();
                    }
                }
            }
        }

        if (oldest_file.empty()) {
            fs::remove_all(oldest_hour_folder);
            INFO_PRINT("Deleted empty hour folder: %s", oldest_hour_folder.string().c_str());
            return;
        }

        fs::remove(oldest_file);
        INFO_PRINT("Deleted file: %s", oldest_file.string().c_str());

        fs::path counterpart = oldest_file;
        counterpart.replace_extension(oldest_file.extension() == ".mp4" ? ".jpg" : ".mp4");
        if (fs::remove(counterpart)) {
            INFO_PRINT("Deleted counterpart file: %s", counterpart.string().c_str());
        }

        // clean up empty folders
        if (fs::is_empty(oldest_hour_folder)) {
            fs::remove(oldest_hour_folder);
            INFO_PRINT("Deleted empty hour folder: %s", oldest_hour_folder.string().c_str());

            if (fs::is_empty(oldest_date_folder)) {
                fs::remove(oldest_date_folder);
                INFO_PRINT("Deleted empty date folder: %s", oldest_date_folder.string().c_str());
            }
        }
    } catch (const fs::filesystem_error &e) {
        ERROR_PRINT("Error while deleting: %s", e.what());
    }
}

/* Builds a timestamped `<root>/<date>/<hour>/<YYYYMMDD_HHMMSS>.<ext>` path. */
class FileInfo {
    std::string root;
    std::string date;
    std::string hour;
    std::string filename;
    std::string extension;

  public:
    FileInfo(const std::string &root, const std::string &extension = "mp4")
        : root(root),
          extension(extension) {
        time_t now = time(0);
        tm ltm_buf{};
        localtime_r(&now, &ltm_buf);
        tm *ltm = &ltm_buf;

        std::string year = PrefixZero(1900 + ltm->tm_year, 4);
        std::string month = PrefixZero(1 + ltm->tm_mon, 2);
        std::string day = PrefixZero(ltm->tm_mday, 2);
        std::string hour = PrefixZero(ltm->tm_hour, 2);
        std::string min = PrefixZero(ltm->tm_min, 2);
        std::string sec = PrefixZero(ltm->tm_sec, 2);

        std::stringstream filenameStream;
        filenameStream << year << month << day << "_" << hour << min << sec;
        filenameStream >> filename;

        date = year + month + day;
        this->hour = hour;
    }

    std::string GetFullPath() const {
        return fs::path(root) / fs::path(date) / fs::path(hour) / (filename + "." + extension);
    }

    std::string GetFolderPath() const { return fs::path(root) / fs::path(date) / fs::path(hour); }
};

} // namespace

const int ROTATION_PERIOD = 60;
const unsigned long MIN_FREE_BYTE = 400 * 1024 * 1024;
const char *CONTAINER_FORMAT = "mp4";
const char *PREVIEW_IMAGE_EXTENSION = ".jpg";

std::unique_ptr<RecorderManager> RecorderManager::Create(std::shared_ptr<VideoCapturer> video_src,
                                                         std::shared_ptr<AudioCapturer> audio_src,
                                                         Args config, bool auto_start) {
    auto instance = std::make_unique<RecorderManager>(config);
    instance->auto_start_ = auto_start;

    if (video_src) {
        instance->CreateVideoRecorder(video_src);
        instance->SubscribeVideoSource(video_src);
    }
    if (audio_src) {
        instance->CreateAudioRecorder(audio_src);
        instance->SubscribeAudioSource(audio_src);
    }

    instance->StartRotationThread();

    return instance;
}

void RecorderManager::StartRotationThread() {
    rotation_abort_.store(false);
    rotation_requested_.store(false);
    rotation_thread_ = std::thread([this]() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(rotation_mtx_);
                rotation_cv_.wait_for(lock, std::chrono::seconds(ROTATION_PERIOD), [this]() {
                    return rotation_abort_.load() || rotation_requested_.load();
                });
            }
            if (rotation_abort_.load())
                break;
            rotation_requested_.store(false);
            DEBUG_PRINT("Rotate files in path: %s", config.record_path.c_str());
            while (!rotation_abort_.load() && !CheckDriveSpace(config.record_path, MIN_FREE_BYTE)) {
                RotateFiles(config.record_path);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

void RecorderManager::CreateVideoRecorder(std::shared_ptr<VideoCapturer> capturer) {
    video_src_ = capturer;
    fps = capturer->fps();
    width = capturer->width(config.record_stream_idx);
    height = capturer->height(config.record_stream_idx);
    video_recorder = ([this, capturer]() -> std::unique_ptr<VideoRecorder> {
        if (config.record_type == RecordType::Snapshot) {
            return nullptr;
        }
        if (capturer->format() == V4L2_PIX_FMT_H264) {
            return RawH264Recorder::Create(width, height, fps);
        } else if (config.hw_accel) {
#if defined(USE_RPI_HW_ENCODER)
            return V4L2H264Recorder::Create(width, height, fps);
#elif defined(USE_JETSON_HW_ENCODER)
            return JetsonRecorder::Create(width, height, fps);
#endif
        }
        return Openh264Recorder::Create(width, height, fps);
    })();
}

void RecorderManager::CreateAudioRecorder(std::shared_ptr<AudioCapturer> audio_src) {
    audio_recorder = ([this, &audio_src]() -> std::unique_ptr<AudioRecorder> {
        if (config.record_type == RecordType::Snapshot) {
            return nullptr;
        } else {
            return AudioRecorder::Create(audio_src->sample_rate());
        }
    })();
}

RecorderManager::RecorderManager(Args config)
    : config(config),
      fmt_ctx(nullptr),
      auto_start_(true),
      header_written_(false),
      base_start_time_{},
      is_recording_(false),
      record_path(config.record_path) {}

void RecorderManager::SubscribeVideoSource(std::shared_ptr<VideoCapturer> video_src) {
    video_subscription_ = video_src->Subscribe(
        [this](V4L2FrameBufferRef buffer) {
            bool is_keyframe = (buffer->flags() & V4L2_BUF_FLAG_KEYFRAME) ||
                               (video_src_->format() != V4L2_PIX_FMT_H264);

            // waiting first keyframe to start recorders.
            if (auto_start_ && !is_recording_ && is_keyframe) {
                Start();
            }

            if (!is_recording_) {
                return;
            }

            if (file_number_ == 0) {
                base_start_time_ = buffer->timestamp();
                file_number_ = 1;
            }

            int64_t total_elapsed_us =
                (int64_t)(buffer->timestamp().tv_sec - base_start_time_.tv_sec) * 1000000LL +
                (int64_t)(buffer->timestamp().tv_usec - base_start_time_.tv_usec);
            double total_elapsed_time = total_elapsed_us / 1e6;

            if (total_elapsed_time >= file_number_ * config.file_duration && is_keyframe) {
                CloseCurrentFile();
                Start();
                ++file_number_;
            }

            if (video_recorder) {
                video_recorder->OnBuffer(buffer);
            }
        },
        config.record_stream_idx);

    if (video_recorder) {
        video_recorder->OnPacketed([this](AVPacket *pkt) {
            this->WriteIntoFile(pkt);
        });
    }
}

void RecorderManager::SubscribeAudioSource(std::shared_ptr<AudioCapturer> audio_src) {
    if (!audio_recorder) {
        return;
    }

    audio_subscription_ = audio_src->Subscribe([this](AudioBuffer buffer) {
        if (is_recording_) {
            audio_recorder->OnBuffer(buffer);
        }
    });

    audio_recorder->OnPacketed([this](AVPacket *pkt) {
        this->WriteIntoFile(pkt);
    });
}

void RecorderManager::WriteIntoFile(AVPacket *pkt) {
    std::lock_guard<std::mutex> lock(ctx_mux);

    if (!fmt_ctx || !is_recording_)
        return;

    if (!header_written_) {
        if (!(pkt->flags & AV_PKT_FLAG_KEY)) {
            return;
        }

        AVCodecParameters *codecpar = fmt_ctx->streams[pkt->stream_index]->codecpar;

        if (codecpar->codec_id == AV_CODEC_ID_AV1) {
            av_free(codecpar->extradata);
            codecpar->extradata = (uint8_t *)av_malloc(pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(codecpar->extradata, pkt->data, pkt->size);
            memset(codecpar->extradata + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            codecpar->extradata_size = pkt->size;
        }

        if (avformat_write_header(fmt_ctx, nullptr) < 0) {
            ERROR_PRINT("Error writing header, close context to avoid corrupt muxer state");
            avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
            return;
        }
        header_written_ = true;
    }

    int ret;
    if (fmt_ctx->nb_streams > pkt->stream_index &&
        (ret = av_interleaved_write_frame(fmt_ctx, pkt)) < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err_buf, sizeof(err_buf));
        fprintf(stderr, "Error occurred: %s\n", err_buf);
    }
}

void RecorderManager::Start() {
    if (!CheckDriveSpace(record_path, MIN_FREE_BYTE)) {
        rotation_requested_.store(true);
        rotation_cv_.notify_one();
    }

    FileInfo new_file(record_path, CONTAINER_FORMAT);
    auto folder = new_file.GetFolderPath();
    utils::CreateFolder(folder);
    current_filepath_ = new_file.GetFullPath();

    if (config.record_type != RecordType::Snapshot) {
        std::lock_guard<std::mutex> lock(ctx_mux);
        if (avformat_alloc_output_context2(&fmt_ctx, nullptr, CONTAINER_FORMAT,
                                           current_filepath_.c_str()) < 0) {
            ERROR_PRINT("Could not alloc output context");
            fmt_ctx = nullptr;
            usleep(1000);
            return;
        }

        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE) &&
            avio_open(&fmt_ctx->pb, current_filepath_.c_str(), AVIO_FLAG_WRITE) < 0) {
            ERROR_PRINT("Could not open %s", current_filepath_.c_str());
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
            usleep(1000);
            return;
        }

        if (video_recorder) {
            video_recorder->AddStream(fmt_ctx);
        }
        if (audio_recorder) {
            audio_recorder->AddStream(fmt_ctx);
        }

        header_written_ = false;

        av_dump_format(fmt_ctx, 0, current_filepath_.c_str(), 1);
    }

    if (video_recorder) {
        video_recorder->Start();
    }
    if (audio_recorder) {
        audio_recorder->Start();
    }

    if (config.record_type != RecordType::Video) {
        auto image_path = ReplaceExtension(current_filepath_, PREVIEW_IMAGE_EXTENSION);
        MakePreviewImage(image_path);
    }

    is_recording_ = true;
}

void RecorderManager::CloseCurrentFile() {
    is_recording_ = false;
    current_filepath_.clear();

    if (video_recorder) {
        video_recorder->Stop();
    }
    if (audio_recorder) {
        audio_recorder->Stop();
    }

    {
        std::lock_guard<std::mutex> lock(ctx_mux);
        if (fmt_ctx) {
            // av_write_trailer walks muxer state that only avformat_write_header allocates.
            if (header_written_) {
                av_write_trailer(fmt_ctx);
            } else {
                ERROR_PRINT("Closing a container with no header written; the file is empty.");
            }

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&fmt_ctx->pb);
            }
            avformat_free_context(fmt_ctx);
            fmt_ctx = nullptr;
        }
        header_written_ = false;
    }
}

void RecorderManager::Stop() {
    CloseCurrentFile();

    file_number_ = 0;
}

RecorderManager::~RecorderManager() {
    printf("~RecorderManager\n");
    Stop();
    rotation_abort_.store(true);
    rotation_cv_.notify_one();
    if (rotation_thread_.joinable()) {
        rotation_thread_.join();
    }
    video_recorder.reset();
    audio_recorder.reset();
}

std::string RecorderManager::current_filepath() const { return current_filepath_; }

bool RecorderManager::is_recording() const { return is_recording_.load(); }

void RecorderManager::MakePreviewImage(std::string path) {
    // Capture video_src_ by value (shared_ptr copy) so the thread holds its own
    // reference, preventing use-after-free if RecorderManager is destroyed
    // before the 3-second delay completes.
    auto video_src = video_src_;
    auto record_stream_idx = config.record_stream_idx;
    auto jpeg_quality = config.jpeg_quality;
    std::thread([video_src, path, record_stream_idx, jpeg_quality]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!video_src) {
            return;
        }
        auto i420buff = video_src->GetI420Frame(record_stream_idx);
        jpeg_util::CreateJpegImage(i420buff->DataY(), i420buff->width(), i420buff->height(), path,
                                   jpeg_quality);
    }).detach();
}

std::string RecorderManager::ReplaceExtension(const std::string &url,
                                              const std::string &new_extension) {
    size_t last_dot_pos = url.find_last_of('.');
    if (last_dot_pos == std::string::npos) {
        // No extension found, append the new extension
        return url + new_extension;
    } else {
        // Replace the existing extension
        return url.substr(0, last_dot_pos) + new_extension;
    }
}
