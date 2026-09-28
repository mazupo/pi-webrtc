// Build and run:
//   g++ -std=c++17 -I src -o /tmp/t test/test_ipc_endpoints.cpp src/ipc/ipc_endpoint.cpp \
//       src/ipc/endpoint_registry.cpp src/ipc/unix_socket_server.cpp -lpthread && /tmp/t
//
// Covers what IpcChannel relies on when it demuxes Packet.ipc:
//   1. an unserved endpoint name is refused, and never creates a socket
//   2. the default endpoint is byte-for-byte passthrough, as its consumers still expect
//   3. every endpoint hears the remote_id of a payload and when that remote is gone
//   4. socket writes on the default endpoint reach every registered callback

#include "ipc/endpoint_registry.h"
#include "ipc/ipc_endpoint.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const char *kDefaultPath = "/tmp/test-ep-default.sock";
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

// Records what reaches it, standing in for the gamepad endpoint.
class RecordingEndpoint : public UnixSocketEndpoint {
  public:
    const char *name() const override { return "gamepad"; }
    void Start() override {}
    void Stop() override {}
    void Write(const std::string &remote_id, const std::string &payload) override {
        writes.push_back(remote_id + ":" + payload);
    }
    void OnRemoteClosed(const std::string &remote_id) override { closed.push_back(remote_id); }

    std::vector<std::string> writes;
    std::vector<std::string> closed;
};

} // namespace

int main() {
    unlink(kDefaultPath);

    auto raw = IpcEndpoint::Create(kDefaultPath);
    auto recorder = std::make_shared<RecordingEndpoint>();

    EndpointRegistry endpoints;
    endpoints.Add(raw);
    endpoints.Add(recorder);
    endpoints.StartAll();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "[1] an endpoint this device does not serve" << std::endl;
    Check(!endpoints.Write("telemetry", "peer", "should go nowhere"), "writing to it is refused");
    Check(access("/tmp/telemetry", F_OK) != 0 && access("telemetry", F_OK) != 0,
          "and no socket was created for it");

    std::cout << "[2] the default endpoint stays byte-for-byte passthrough" << std::endl;
    int def_fd = ConnectClient(kDefaultPath);
    Check(def_fd >= 0, "client connected to the default socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string plain = "hello, unframed world";
    Check(endpoints.Write(IpcEndpoint::kName, "peer", plain), "write accepted");
    std::vector<char> got(plain.size());
    Check(ReadExactly(def_fd, got.data(), plain.size()), "bytes arrived");
    Check(std::string(got.data(), plain.size()) == plain, "with nothing added around them");

    std::cout << "[3] endpoints hear the remote_id and when it is gone" << std::endl;
    Check(endpoints.Write("gamepad", "alice", "pad"), "write to gamepad accepted");
    Check(recorder->writes == std::vector<std::string>{"alice:pad"},
          "the endpoint got the payload with its remote_id");
    endpoints.OnRemoteClosed("alice");
    Check(recorder->closed == std::vector<std::string>{"alice"},
          "and was told when the remote closed");

    std::cout << "[4] socket writes reach every registered callback" << std::endl;
    std::mutex relayed_mutex;
    std::string relayed;
    endpoints.RegisterMessageCallback("channel", [&](const std::string &msg) {
        std::lock_guard<std::mutex> lock(relayed_mutex);
        relayed += msg;
    });
    const std::string reply = "from the device";
    Check(::write(def_fd, reply.data(), reply.size()) == static_cast<ssize_t>(reply.size()),
          "client wrote to the default socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(relayed_mutex);
        Check(relayed == reply, "the registered callback got it");
    }
    endpoints.UnregisterMessageCallback("channel");

    close(def_fd);
    endpoints.StopAll();

    std::cout << (g_failures == 0 ? "\nALL PASSED" : "\nFAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
