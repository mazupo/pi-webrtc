#ifndef COMMON_LOGGING_H_
#define COMMON_LOGGING_H_

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace logging {

inline std::string GetFolderName(const std::string &file_path) {
    return std::filesystem::path(file_path).parent_path().filename().string();
}

inline std::string GetFileName(const std::string &file_path) {
    return std::filesystem::path(file_path).filename().string();
}

inline std::string GetCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm local_tm{};
    localtime_r(&t, &local_tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &local_tm);
    char result[24];
    std::snprintf(result, sizeof(result), "%s.%03lld", buf, static_cast<long long>(ms.count()));
    return result;
}

} // namespace logging

#ifdef DEBUG_MODE
#define DEBUG_PRINT(fmt, ...)                                                                      \
    printf("[%s] [%d] \033[1;33mDEBUG\033[0m \033[1;37m%s\033[0m \033[1;34m%s:%d\033[0m " fmt      \
           "\n",                                                                                   \
           logging::GetCurrentTime().c_str(), (int)gettid(),                                       \
           logging::GetFolderName(__FILE__).c_str(), logging::GetFileName(__FILE__).c_str(),       \
           __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

#define ERROR_PRINT(fmt, ...)                                                                      \
    fprintf(stderr,                                                                                \
            "[%s] [%d] \033[1;31mERROR\033[0m \033[1;37m%s\033[0m \033[1;34m%s:%d\033[0m " fmt     \
            "\n",                                                                                  \
            logging::GetCurrentTime().c_str(), (int)gettid(),                                      \
            logging::GetFolderName(__FILE__).c_str(), logging::GetFileName(__FILE__).c_str(),      \
            __LINE__, ##__VA_ARGS__)

#define WARN_PRINT(fmt, ...)                                                                       \
    fprintf(stderr,                                                                                \
            "[%s] [%d] \033[1;35m WARN\033[0m \033[1;37m%s\033[0m \033[1;34m%s:%d\033[0m " fmt     \
            "\n",                                                                                  \
            logging::GetCurrentTime().c_str(), (int)gettid(),                                      \
            logging::GetFolderName(__FILE__).c_str(), logging::GetFileName(__FILE__).c_str(),      \
            __LINE__, ##__VA_ARGS__)

#define INFO_PRINT(fmt, ...)                                                                       \
    printf("[%s] [%d] \033[1;32m INFO\033[0m \033[1;37m%s\033[0m \033[1;34m%s:%d\033[0m " fmt      \
           "\n",                                                                                   \
           logging::GetCurrentTime().c_str(), (int)gettid(),                                       \
           logging::GetFolderName(__FILE__).c_str(), logging::GetFileName(__FILE__).c_str(),       \
           __LINE__, ##__VA_ARGS__)

#endif // COMMON_LOGGING_H_
