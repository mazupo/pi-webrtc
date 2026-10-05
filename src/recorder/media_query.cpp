#include "recorder/media_query.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <third_party/libyuv/include/libyuv.h>

#include "common/jpeg_util.h"
#include "common/logging.h"

namespace fs = std::filesystem;

namespace media_query {

namespace {

std::string ToBase64(const std::string &binary_file) {
    std::string out;
    int val = 0, valb = -6;
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (unsigned char c : binary_file) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

std::vector<std::pair<fs::file_time_type, fs::path>> GetFiles(const std::string &path,
                                                              const std::string &extension) {
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return files;
    }
    for (const auto &entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            files.emplace_back(fs::last_write_time(entry), entry.path());
        }
    }
    return files;
}

std::string FindLatestSubDir(const std::string &path) {
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return "";
    }

    std::string latestDir;

    // folders named in "yyyyMMdd" or "hh"
    std::regex datePattern("^([0-9]{8}|[0-9]{2})$");

    for (const auto &entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::string folderName = entry.path().filename().string();
            if (std::regex_match(folderName, datePattern)) {
                if (folderName > latestDir) {
                    latestDir = folderName;
                }
            }
        }
    }
    return latestDir;
}

std::string GetPreviousDate(const std::string &dateStr) {
    std::tm tm = {};
    std::istringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y%m%d");

    tm.tm_mday -= 1;
    mktime(&tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d");

    return oss.str();
}

std::chrono::system_clock::time_point ParseDatetime(const std::string &datetime_str) {
    std::tm tm = {};
    std::stringstream ss(datetime_str);
    ss >> std::get_time(&tm, "%Y%m%d_%H%M%S");
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace

std::string FindLatestCompleteFile(const std::string &base_dir, const std::string &extension) {
    std::string latestDateDir = FindLatestSubDir(base_dir);

    // Flat structure: no date subdirectories, files reside directly in base_dir.
    // All files are complete (no actively-recording file), so return the actual newest.
    if (latestDateDir.empty()) {
        auto files = GetFiles(base_dir, extension);
        std::sort(files.begin(), files.end(), std::greater<>());
        if (files.empty()) {
            return "";
        }
        return files[0].second.string();
    }

    // Nested structure: base_dir/YYYYMMDD/HH/
    std::string datePath = (fs::path(base_dir) / latestDateDir).string();
    std::string latestHourDir = FindLatestSubDir(datePath);
    if (latestHourDir.empty()) {
        std::cerr << "No hour directories found." << std::endl;
        return "";
    }

    std::string latestDir = (fs::path(datePath) / latestHourDir).string();
    auto files = GetFiles(latestDir, extension);

    // find previous hour (scan for the latest existing dir before latestHourDir)
    if (files.size() < 2) {
        std::string prevHourDir;
        for (const auto &e : fs::directory_iterator(datePath)) {
            if (e.is_directory()) {
                std::string name = e.path().filename().string();
                if (name < latestHourDir && (prevHourDir.empty() || name > prevHourDir)) {
                    prevHourDir = name;
                }
            }
        }
        if (!prevHourDir.empty()) {
            std::string prevDir = (fs::path(datePath) / prevHourDir).string();
            auto prevFiles = GetFiles(prevDir, extension);
            files.insert(files.end(), prevFiles.begin(), prevFiles.end());
        }
    }

    // find previous date
    if (files.size() < 2) {
        std::string prevDateDir = GetPreviousDate(latestDateDir);

        std::string prevDatePath = (fs::path(base_dir) / prevDateDir).string();
        latestHourDir = FindLatestSubDir(prevDatePath);
        std::string prevDir = (fs::path(prevDatePath) / latestHourDir).string();
        auto prevFiles = GetFiles(prevDir, extension);

        files.insert(files.end(), prevFiles.begin(), prevFiles.end());
    }

    std::sort(files.begin(), files.end(), std::greater<>());

    if (files.size() < 2) {
        std::cerr << "Not enough files to determine the second newest file." << std::endl;
        return "";
    }

    return files[1].second.string();
}

std::string FindFilesFromDatetime(const std::string &root, const std::string &basename) {
    if (basename.length() < 15) {
        return "";
    }

    std::string date = basename.substr(0, 8);
    std::string time = basename.substr(9);
    std::string hour = time.substr(0, 2);

    bool is_nested = false;
    std::regex date_dir_pattern("^[0-9]{8}$");
    if (fs::exists(root) && fs::is_directory(root)) {
        for (const auto &e : fs::directory_iterator(root)) {
            if (e.is_directory() &&
                std::regex_match(e.path().filename().string(), date_dir_pattern)) {
                is_nested = true;
                break;
            }
        }
    }

    // Flat structure: no nested date/hour dirs — scan root directly
    if (!is_nested) {
        auto time_limit = ParseDatetime(basename);
        auto files = GetFiles(root, ".mp4");
        std::sort(files.begin(), files.end(), std::greater<>());
        for (auto &p : files) {
            if (fs::file_time_type::clock::to_sys(p.first) < time_limit) {
                return p.second.string();
            }
        }
        return "";
    }

    auto time_limit = ParseDatetime(basename);

    fs::path hour_path = fs::path(root) / date / hour;

    int max_searching_folder = 10;
    for (int count = 0; count < max_searching_folder; count++) {
        // find in the same hour
        auto hour_files = GetFiles(hour_path.string(), ".mp4");
        std::sort(hour_files.begin(), hour_files.end(), std::greater<>());

        for (auto &p : hour_files) {
            if (fs::file_time_type::clock::to_sys(p.first) < time_limit) {
                return p.second.string();
            }
        }

        fs::path date_path = hour_path.parent_path();
        std::string cur_hour = hour_path.filename().string();

        // Find the latest existing hour dir before cur_hour in the same date
        std::string prev_hour;
        if (fs::is_directory(date_path)) {
            for (const auto &e : fs::directory_iterator(date_path)) {
                if (e.is_directory()) {
                    std::string name = e.path().filename().string();
                    if (name < cur_hour && (prev_hour.empty() || name > prev_hour)) {
                        prev_hour = name;
                    }
                }
            }
        }
        if (!prev_hour.empty()) {
            hour_path = date_path / prev_hour;
        } else {
            // No earlier hour in this date, move to previous date
            fs::path root_path = date_path.parent_path();
            std::string date = date_path.filename();
            auto prev_date = GetPreviousDate(date);
            date_path = root_path / prev_date;
            if (!fs::exists(date_path)) {
                ERROR_PRINT("pre date path %s is not found", date_path.string().c_str());
                break;
            }
            std::string latest_hour = FindLatestSubDir(date_path.string());
            if (latest_hour.empty()) {
                break;
            }
            hour_path = date_path / latest_hour;
        }
    }

    return "";
}

std::vector<std::string> FindOlderFiles(const std::string &base_dir, const std::string &file_path,
                                        int request_num) {
    std::vector<std::string> result;
    fs::path file(file_path);
    if (!fs::exists(file)) {
        return result;
    }
    auto file_last_write_time = fs::last_write_time(file);
    auto extension = file.extension();

    // Flat structure: file resides directly in base_dir
    fs::path base(base_dir);
    if (std::error_code ec; fs::equivalent(file.parent_path(), base, ec)) {
        auto files = GetFiles(base_dir, extension.string());
        std::sort(files.begin(), files.end(), std::greater<>());
        for (auto &p : files) {
            if (p.first < file_last_write_time) {
                result.push_back(p.second.string());
                if (result.size() == static_cast<size_t>(request_num))
                    break;
            }
        }
        return result;
    }

    // Nested structure: base_dir/YYYYMMDD/HH/
    fs::path hour_path = file.parent_path();
    fs::path date_path = hour_path.parent_path();
    fs::path root_path = date_path.parent_path();

    while (result.size() < static_cast<size_t>(request_num)) {
        // find in the same hour
        auto files = GetFiles(hour_path.string(), extension.string());
        std::sort(files.begin(), files.end(), std::greater<>());

        for (auto &p : files) {
            if (p.first < file_last_write_time) {
                result.push_back(p.second.string());
                if (result.size() == static_cast<size_t>(request_num)) {
                    return result;
                }
            }
        }

        std::string hour = hour_path.filename();

        // Find the latest existing hour dir before current hour in the same date
        std::string prev_hour;
        if (fs::is_directory(date_path)) {
            for (const auto &e : fs::directory_iterator(date_path)) {
                if (e.is_directory()) {
                    std::string name = e.path().filename().string();
                    if (name < hour && (prev_hour.empty() || name > prev_hour)) {
                        prev_hour = name;
                    }
                }
            }
        }
        if (!prev_hour.empty()) {
            hour_path = date_path / prev_hour;
        } else {
            // No earlier hour in this date, move to previous date's latest hour
            std::string date = date_path.filename();
            auto prev_date = GetPreviousDate(date);
            date_path = root_path / prev_date;
            if (!fs::exists(date_path)) {
                ERROR_PRINT("pre date path %s is not found", date_path.string().c_str());
                break;
            }
            std::string latest_hour = FindLatestSubDir(date_path.string());
            if (latest_hour.empty()) {
                break;
            }
            hour_path = date_path / latest_hour;
        }
    }

    return result;
}

uint32_t GetVideoDuration(const std::string &filePath) {
    AVFormatContext *formatContext = nullptr;
    if (avformat_open_input(&formatContext, filePath.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "Could not open file: " << filePath << std::endl;
        return 0;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&formatContext);
        return 0;
    }

    int64_t duration = formatContext->duration;
    int durationInSeconds = static_cast<int>(duration / AV_TIME_BASE);

    avformat_close_input(&formatContext);

    return durationInSeconds;
}

std::string GetThumbnailBase64(const std::string &file_path, int scale_denom, int quality) {
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    std::string result;

    auto cleanup = [&]() {
        if (frame)
            av_frame_free(&frame);
        if (pkt)
            av_packet_free(&pkt);
        if (codec_ctx)
            avcodec_free_context(&codec_ctx);
        if (fmt_ctx)
            avformat_close_input(&fmt_ctx);
    };

    if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0) {
        return "";
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        cleanup();
        return "";
    }

    int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx < 0) {
        cleanup();
        return "";
    }

    AVStream *video_stream = fmt_ctx->streams[video_stream_idx];
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        cleanup();
        return "";
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        cleanup();
        return "";
    }

    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        cleanup();
        return "";
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        cleanup();
        return "";
    }

    if (fmt_ctx->duration > 0) {
        int64_t seek_ts = fmt_ctx->duration / 10;
        av_seek_frame(fmt_ctx, -1, seek_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(codec_ctx);
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        cleanup();
        return "";
    }

    bool got_frame = false;
    int max_packets = 200;
    while (av_read_frame(fmt_ctx, pkt) >= 0 && !got_frame && max_packets-- > 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    got_frame = true;
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (!got_frame) {
        cleanup();
        return "";
    }

    // Recordings are 4:2:0 (H264 or AV1); other layouts get no thumbnail.
    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
        cleanup();
        return "";
    }

    int src_w = frame->width;
    int src_h = frame->height;
    int dst_w = std::max(2, (src_w / scale_denom) & ~1);
    int dst_h = std::max(2, (src_h / scale_denom) & ~1);

    std::vector<uint8_t> i420(dst_w * dst_h * 3 / 2);
    uint8_t *dst_y = i420.data();
    uint8_t *dst_u = dst_y + dst_w * dst_h;
    uint8_t *dst_v = dst_u + dst_w * dst_h / 4;
    libyuv::I420Scale(frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1],
                      frame->data[2], frame->linesize[2], src_w, src_h, dst_y, dst_w, dst_u,
                      dst_w / 2, dst_v, dst_w / 2, dst_w, dst_h, libyuv::kFilterBilinear);

    auto jpeg = jpeg_util::ConvertYuvToJpeg(i420.data(), dst_w, dst_h, quality);
    if (jpeg.start && jpeg.length > 0) {
        result = ToBase64(std::string(reinterpret_cast<char *>(jpeg.start.get()), jpeg.length));
    }

    cleanup();
    return result;
}
} // namespace media_query
