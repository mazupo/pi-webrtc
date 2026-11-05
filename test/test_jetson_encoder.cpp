#include "capturer/libargus_capturer.h"
#include "codecs/jetson/jetson_encoder.h"

#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>

int main(int argc, char *argv[]) {
    std::mutex mtx;
    std::condition_variable cond_var;
    bool is_finished = false;
    bool has_first_keyframe_ = false;
    int images_nb = 0;
    int record_sec = 100;
    VideoCaptureConfig config{
        .camera_id = 0,
        .width = 1280,
        .height = 720,
        .fps = 60,
        .hw_accel = false, // hw will use dma fd
    };

    auto capturer = LibargusCapturer::Create(config);
    auto encoder = JetsonEncoder::Create(
        {.width = config.width, .height = config.height, .dst_pix_fmt = V4L2_PIX_FMT_H264});

    int cam_frame_count = 0;
    auto cam_start_time = std::chrono::steady_clock::now();
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        auto cam_current_time = std::chrono::steady_clock::now();
        cam_frame_count++;

        if (std::chrono::duration_cast<std::chrono::seconds>(cam_current_time - cam_start_time)
                .count() >= 1) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(cam_current_time -
                                                                                  cam_start_time)
                                .count();
            double cam_current_fps = static_cast<double>(cam_frame_count) / (duration / 1000.0);
            cam_start_time = cam_current_time;
            printf("Camera FPS: %.2f\n", cam_current_fps);
            cam_frame_count = 0;
        }

        encoder->EmplaceBuffer(frame_buffer, [&](V4L2FrameBufferRef encoded_buffer) {
            if (is_finished) {
                return;
            }

            auto current_time = std::chrono::steady_clock::now();
            frame_count++;

            if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time)
                    .count() >= 1) {
                auto duration =
                    std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time)
                        .count();
                double current_fps = static_cast<double>(frame_count) / (duration / 1000.0);
                start_time = current_time;
                printf("Codec FPS: %.2f\n", current_fps);
                frame_count = 0;
            }

            if (images_nb++ > config.fps * record_sec) {
                is_finished = true;
                cond_var.notify_all();
            }
        });
    });

    std::unique_lock<std::mutex> lock(mtx);
    cond_var.wait(lock, [&] {
        return is_finished;
    });

    return 0;
}
