#include "args.h"
#include "capturer/v4l2_capturer.h"
#include "codecs/jetson/jetson_decoder.h"

#include <condition_variable>
#include <mutex>
#include <string>

void WriteYuvImage(const uint8_t *start, int length, int index) {
    std::string filename = "img" + std::to_string(index) + ".yuv";
    FILE *file = fopen(filename.c_str(), "wb");
    if (file) {
        fwrite(start, 1, length, file);
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
    Args args{.fps = 30,
              .width = 160,
              .height = 120,
              .format = V4L2_PIX_FMT_MJPEG,
              .camera_id = 2,
              .hw_accel = false};

    auto capturer = V4L2Capturer::Create(args);
    auto decoder = JetsonDecoder::Create({args.width, args.height, capturer->format(), true});
    if (!decoder) {
        fprintf(stderr, "Failed to create the jetson decoder\n");
        return 1;
    }

    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        printf("Camera buffer: %u\n", frame_buffer->size());

        decoder->EmplaceBuffer(frame_buffer, [&](V4L2FrameBufferRef decoded_buffer) {
            if (is_finished) {
                return;
            }

            if (images_nb++ < args.fps * record_sec) {
                auto i420 = decoded_buffer->ToI420();
                long sum = 0;
                for (int i = 0; i < i420->width() * i420->height(); i++) {
                    sum += i420->DataY()[i];
                }
                double mean = (double)sum / (i420->width() * i420->height());
                printf("Buffer index: %d\n  dma fd: %d\n  %dx%d\n  mean luma: %.1f%s\n", images_nb,
                       decoded_buffer->GetDmaFd(), i420->width(), i420->height(), mean,
                       sum == 0 ? "  <-- ALL BLACK" : "");
                WriteYuvImage(i420->DataY(), args.width * args.height * 3 / 2, images_nb);
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

    capturer.reset();
    decoder.reset();

    return 0;
}
