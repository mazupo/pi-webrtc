#ifndef SIGNALING_CONFIG_H_
#define SIGNALING_CONFIG_H_

#include <cstdint>
#include <string>
#include <vector>

struct MqttConfig {
    std::string host = "localhost";
    int port = 1883;
    std::string username = "";
    std::string password = "";
    std::string uid = "";
};

struct WhepConfig {
    uint16_t port = 8080;
};

struct LiveKitConfig {
    std::string url = ""; // e.g. ws://127.0.0.1:7880; the parser splits it into host, port, use_tls
    std::string host = "";
    uint16_t port = 0; // 0 => use_tls ? 443 : 80
    bool use_tls = false;
    std::string room = "";
    std::string key = "";
    std::string uid = "";
    bool enable_ipc = false;
    bool no_audio = false;
};

struct CloudflareConfig {
    std::string api_url = "";
    std::string api_key = "";
    std::vector<std::string> rooms;
    std::string uid = "";
    bool enable_ipc = false;
    bool no_audio = false;
};

#endif // SIGNALING_CONFIG_H_
