#ifndef RECORDER_CONFIG_H_
#define RECORDER_CONFIG_H_

#include <string>

enum RecordType {
    Video = 0,
    Snapshot = 1,
};

struct RecorderConfig {
    std::string path = "";
    int type = -1; // a RecordType, or -1 for both
    int file_duration = 60;
    int stream_idx = 0;
    int jpeg_quality = 30;
    bool hw_accel = false;
};

#endif // RECORDER_CONFIG_H_
