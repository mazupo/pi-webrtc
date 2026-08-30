// Device half of the end-to-end check: serves a gamepad endpoint through pi-webrtc's own
// IpcEndpoints and writes the payloads it is given, exactly as IpcChannel does after it
// demuxes a Packet.ipc. Usage: e2e_writer <socket-path> <payload-hex>...
#include "ipc/ipc_endpoints.h"

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
        std::cerr << "usage: e2e_writer <socket-path> <payload-hex>..." << std::endl;
        return 2;
    }

    unlink(argv[1]);
    IpcEndpoints endpoints;
    endpoints.Add(IpcEndpoints::kGamepad, UnixSocketServer::Create(argv[1]),
                  /*length_prefixed=*/true, /*bidirectional=*/false);
    endpoints.StartAll();

    // Give the consumer time to connect, then send at roughly the 60 Hz the browser uses.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    int sent = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string payload = FromHex(argv[i]);
        if (endpoints.Write(IpcEndpoints::kGamepad, payload)) {
            ++sent;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }

    std::cout << "wrote " << sent << " of " << (argc - 2) << " reports" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    endpoints.StopAll();
    return sent == argc - 2 ? 0 : 1;
}
