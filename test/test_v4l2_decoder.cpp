#include "capturer/v4l2_capturer.h"
#include "codecs/v4l2/v4l2_decoder.h"

#include <condition_variable>
#include <mutex>
#include <string>

void WriteYuvImage(void *start, int length, int index) {
    std::string filename = "img" + std::to_string(index) + ".yuv";
    FILE *file = fopen(filename.c_str(), "wb");
    if (file) {
        fwrite((uint8_t *)start, 1, length, file);
        fclose(file);
    } else {
        fprintf(stderr, "Failed to open file for writing: %s\n", filename.c_str());
    }
}

int main(int argc, char *argv[]) {
    std::mutex mtx;
    std::condition_variable cond_var;
    bool is_finished = false;
    int images_nb = 0;
    int record_sec = 1;
    VideoCaptureConfig config{.camera_id = 0,
                              .format = V4L2_PIX_FMT_MJPEG,
                              .width = 640,
                              .height = 480,
                              .fps = 15,
                              .hw_accel = false};

    auto capturer = V4L2Capturer::Create(config);
    auto decoder = V4L2Decoder::Create({config.width, config.height, capturer->format(), true});

    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        printf("Camera buffer: %u\n", frame_buffer->size());

        decoder->EmplaceBuffer(frame_buffer, [&](V4L2FrameBufferRef decoded_buffer) {
            auto raw_buffer = decoded_buffer->GetRawBuffer();
            if (is_finished) {
                return;
            }

            if (images_nb++ < config.fps * record_sec) {
                printf("Buffer index: %d\n  bytesused: %u\n", images_nb, raw_buffer.length);
                WriteYuvImage(raw_buffer.start, raw_buffer.length, images_nb);
            } else {
                is_finished = true;
                cond_var.notify_all();
            }
        });
    });

    std::unique_lock<std::mutex> lock(mtx);
    cond_var.wait(lock, [&] {
        return is_finished;
    });

    decoder.reset();

    return 0;
}
