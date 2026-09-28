// Device half of the end-to-end check: serves the gamepad socket through pi-webrtc's own
// GamepadEndpoint and feeds it the InputReports it is given, as IpcChannel does after it
// demuxes a Packet.ipc. Built with -DBUILD_TEST=gamepad.
// Usage: e2e-gamepad-writer <socket-path> <payload-hex>...
#include "ipc/gamepad_endpoint.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::string FromHex(const std::string &hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    }
    return out;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: e2e-gamepad-writer <socket-path> <payload-hex>..." << std::endl;
        return 2;
    }

    unlink(argv[1]);
    auto endpoint = GamepadEndpoint::Create(argv[1]);
    endpoint->Start();

    // Give the consumer time to connect, then send at roughly the 60 Hz the browser uses.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    for (int i = 2; i < argc; ++i) {
        endpoint->Write("e2e", FromHex(argv[i]));
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }

    std::cout << "wrote " << (argc - 2) << " reports" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    endpoint->Stop();
    return 0;
}
