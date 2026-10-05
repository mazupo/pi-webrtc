#include "args.h"
#include "capturer/rtsp_capturer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

// Usage: test-rtsp-capturer <rtsp-url> [--hw-accel] [seconds]
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rtsp-url> [--hw-accel] [seconds]\n", argv[0]);
        return 1;
    }

    Args args;
    args.camera = argv[1];
    args.camera_source = CameraSource::Rtsp;
    int seconds = 10;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--hw-accel") {
            args.hw_accel = true;
        } else {
            seconds = std::atoi(argv[i]);
        }
    }

    auto capturer = RtspCapturer::Create(args);
    int report_every = std::max(capturer->fps(), 1);
    std::atomic<int> frames{0};
    std::atomic<std::chrono::steady_clock::rep> first_frame{0};

    {
        auto subscription = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
            int n = ++frames;
            if (n == 1) {
                first_frame = std::chrono::steady_clock::now().time_since_epoch().count();
            }
            if (n % report_every != 1) {
                return;
            }
            auto i420 = frame_buffer->ToI420();
            long sum = 0;
            int pixels = i420->width() * i420->height();
            for (int i = 0; i < pixels; i++) {
                sum += i420->DataY()[i];
            }
            printf("frame %d: %dx%d, dma fd %d, mean luma %.1f\n", n, i420->width(), i420->height(),
                   frame_buffer->GetDmaFd(), static_cast<double>(sum) / pixels);
        });
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }

    if (frames == 0) {
        printf("No frames received.\n");
        return 1;
    }
    auto start = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(first_frame.load()));
    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("%dx%d: %d frames in %.1f s after the first one (%.1f fps), hardware decoder: %s\n",
           capturer->width(), capturer->height(), frames.load(), elapsed, (frames - 1) / elapsed,
           capturer->is_dma_capture() ? "yes" : "no");
    return 0;
}
