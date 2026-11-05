#include "capturer/libcamera_capturer.h"
#include "capturer/pa_capturer.h"
#include "recorder/recorder_manager.h"

int main(int argc, char *argv[]) {
    auto video_capture = LibcameraCapturer::Create(
        {.format = V4L2_PIX_FMT_YUV420, .width = 1280, .height = 720, .fps = 15});
    auto audio_capture = PaCapturer::Create({.sample_rate = 48000});
    auto recorder_mgr = RecorderManager::Create(video_capture, audio_capture, {.path = "./"});
    sleep(5);

    return 0;
}
