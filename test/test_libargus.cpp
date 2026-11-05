#include "capturer/libargus_capturer.h"

#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>

int main() {
    std::mutex mtx;
    std::condition_variable cond_var;
    bool is_finished = false;
    int frame_count = 0;
    int record_sec = 5;
    VideoCaptureConfig config{.width = 1280, .height = 720, .fps = 60};

    auto capturer = LibargusCapturer::Create(config);

    auto start_time = std::chrono::steady_clock::now();
    auto record_start_time = start_time;
    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        if (is_finished) {
            return;
        }

        auto current_time = std::chrono::steady_clock::now();
        frame_count++;

        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count() >=
            1) {
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time)
                    .count();
            double current_fps = static_cast<double>(frame_count) / (duration / 1000.0);
            start_time = current_time;
            printf("Codec FPS: %.2f\n", current_fps);
            frame_count = 0;
        }

        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - record_start_time)
                .count() >= record_sec) {
            is_finished = true;
            cond_var.notify_all();
        }
    });

    std::unique_lock<std::mutex> lock(mtx);
    cond_var.wait(lock, [&] {
        return is_finished;
    });

    return 0;
}
