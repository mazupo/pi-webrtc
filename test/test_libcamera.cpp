#include "capturer/libcamera_capturer.h"

#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

void WriteImage(void *start, int length, int index) {
    printf("Dequeued buffer index: %d\n"
           "  bytesused: %d\n",
           index, length);

    std::string filename = "img" + std::to_string(index) + ".yuv";
    int outfd = open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if ((outfd == -1) && (EEXIST == errno)) {
        /* open the existing file with write flag */
        outfd = open(filename.c_str(), O_WRONLY);
    }

    write(outfd, start, length);
}

int main(int argc, char *argv[]) {
    std::mutex mtx;
    std::condition_variable cond_var;
    bool is_finished = false;
    int i = 0;
    int images_nb = 10;
    VideoCaptureConfig config{.width = 1280, .height = 960, .fps = 30};

    auto capturer = LibcameraCapturer::Create(config);

    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        if (i < images_nb) {
            auto buffer = frame_buffer->GetRawBuffer();
            WriteImage(buffer.start, buffer.length, ++i);
        } else {
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
