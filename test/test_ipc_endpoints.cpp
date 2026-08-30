// Build and run:
//   g++ -std=c++17 -I src -o /tmp/t test/test_ipc_endpoints.cpp \
//       src/ipc/ipc_endpoints.cpp src/ipc/unix_socket_server.cpp -lpthread && /tmp/t
//
// Covers what IpcChannel relies on when it demuxes Packet.ipc:
//   1. an unserved endpoint name is refused, and never creates a socket
//   2. the default endpoint is byte-for-byte passthrough, as its consumers still expect
//   3. the gamepad endpoint frames every payload with a big-endian uint32 length

#include "ipc/ipc_endpoints.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const char *kDefaultPath = "/tmp/test-ep-default.sock";
const char *kGamepadPath = "/tmp/test-ep-gamepad.sock";
int g_failures = 0;

void Check(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) {
        ++g_failures;
    }
}

int ConnectClient(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

bool ReadExactly(int fd, char *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = ::read(fd, buf + got, n - got);
        if (r <= 0) {
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

// The exact read mavlink-proxy performs: readexactly(4), then readexactly(that many).
bool ReadFrame(int fd, std::string *out) {
    char header[4];
    if (!ReadExactly(fd, header, 4)) {
        return false;
    }
    uint32_t length;
    memcpy(&length, header, 4);
    length = ntohl(length);
    std::vector<char> body(length);
    if (length && !ReadExactly(fd, body.data(), length)) {
        return false;
    }
    out->assign(body.data(), length);
    return true;
}

} // namespace

int main() {
    unlink(kDefaultPath);
    unlink(kGamepadPath);

    IpcEndpoints endpoints;
    endpoints.Add(IpcEndpoints::kDefault, UnixSocketServer::Create(kDefaultPath),
                  /*length_prefixed=*/false, /*bidirectional=*/true);
    endpoints.Add(IpcEndpoints::kGamepad, UnixSocketServer::Create(kGamepadPath),
                  /*length_prefixed=*/true, /*bidirectional=*/false);
    endpoints.StartAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "[1] an endpoint this device does not serve" << std::endl;
    Check(endpoints.Find("telemetry") == nullptr, "lookup of an unserved name returns null");
    Check(!endpoints.Write("telemetry", "should go nowhere"), "writing to it is refused");
    Check(access("/tmp/telemetry", F_OK) != 0 && access("telemetry", F_OK) != 0,
          "and no socket was created for it");

    std::cout << "[2] the default endpoint stays byte-for-byte passthrough" << std::endl;
    int def_fd = ConnectClient(kDefaultPath);
    Check(def_fd >= 0, "client connected to the default socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string plain = "hello, unframed world";
    Check(endpoints.Write(IpcEndpoints::kDefault, plain), "write accepted");
    std::vector<char> raw(plain.size());
    Check(ReadExactly(def_fd, raw.data(), plain.size()), "bytes arrived");
    Check(std::string(raw.data(), plain.size()) == plain, "with no length prefix in front of them");

    std::cout << "[3] the gamepad endpoint frames every payload" << std::endl;
    int gp_fd = ConnectClient(kGamepadPath);
    Check(gp_fd >= 0, "client connected to the gamepad socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The sizes a real InputReport occupies, 13..51 bytes, plus the boundaries.
    std::vector<size_t> sizes;
    for (size_t n = 13; n <= 51; ++n) {
        sizes.push_back(n);
    }
    sizes.push_back(0);
    sizes.push_back(70000); // past one read() buffer, to prove the reader reassembles

    bool all_intact = true;
    for (size_t i = 0; i < sizes.size(); ++i) {
        const std::string payload(sizes[i], static_cast<char>('a' + (i % 26)));
        if (!endpoints.Write(IpcEndpoints::kGamepad, payload)) {
            all_intact = false;
            break;
        }
        std::string got;
        if (!ReadFrame(gp_fd, &got) || got != payload) {
            all_intact = false;
            break;
        }
    }
    Check(all_intact, "all " + std::to_string(sizes.size()) +
                          " frames read back intact via readexactly(4) + readexactly(n)");

    std::cout << "[4] the two endpoints are separate sockets" << std::endl;
    Check(endpoints.Write(IpcEndpoints::kGamepad, "for gamepad only"), "write to gamepad");
    std::string got;
    Check(ReadFrame(gp_fd, &got) && got == "for gamepad only", "gamepad client got it");

    // Nothing should be waiting on the default socket. A non-blocking read proves it.
    int flags = fcntl(def_fd, F_GETFL, 0);
    fcntl(def_fd, F_SETFL, flags | O_NONBLOCK);
    char stray[64];
    ssize_t n = ::read(def_fd, stray, sizeof(stray));
    Check(n < 0, "and nothing leaked onto the default socket");

    close(def_fd);
    close(gp_fd);
    endpoints.StopAll();

    std::cout << (g_failures == 0 ? "\nALL PASSED" : "\nFAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
