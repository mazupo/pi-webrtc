#include "capturer/v4l2_capturer.h"

#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

void WriteImage(V4L2Buffer buffer, int index) {
    printf("Dequeue buffer index: %d\n"
           "  bytesused: %d\n",
           index, buffer.length);

    std::string filename = "img" + std::to_string(index) + ".jpg";
    int outfd = open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if ((outfd == -1) && (EEXIST == errno)) {
        /* open the existing file with write flag */
        outfd = open(filename.c_str(), O_WRONLY);
    }

    write(outfd, buffer.start, buffer.length);
}

int main(int argc, char *argv[]) {
    std::mutex mtx;
    std::condition_variable cond_var;
    bool is_finished = false;
    int i = 0;
    int images_nb = 10;
    VideoCaptureConfig config{.camera_id = 0,
                              .format = V4L2_PIX_FMT_MJPEG,
                              .width = 1280,
                              .height = 720,
                              .fps = 15,
                              .hw_accel = true};

    auto capturer = V4L2Capturer::Create(config);
    auto observer = capturer->Subscribe([&](V4L2FrameBufferRef frame_buffer) {
        if (i < images_nb) {
            WriteImage(frame_buffer->GetRawBuffer(), ++i);
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
