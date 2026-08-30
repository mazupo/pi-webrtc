// Build and run:
//   g++ -std=c++17 -I src -o /tmp/t test/test_unix_socket_write.cpp src/ipc/unix_socket_server.cpp
//   -lpthread && /tmp/t

// Regression test for the UnixSocketServer write path.
//
//   1. framing survives a long run of length-prefixed messages
//   2. a consumer that stops reading no longer stalls a healthy one
//
// Before the fix (2) hangs forever: Write() blocked on the stalled client while holding
// the mutex, with no send timeout to break it out.

#include "ipc/unix_socket_server.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const char *kPath = "/tmp/pi-webrtc-test-ipc.sock";
int g_failures = 0;

void Check(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) {
        ++g_failures;
    }
}

std::string Frame(const std::string &payload) {
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::string out(reinterpret_cast<const char *>(&len), 4);
    return out + payload;
}

int ConnectClient() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, kPath, sizeof(addr.sun_path) - 1);
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

// Reads `count` framed messages and reports how many arrived intact and in order.
int DrainFrames(int fd, int count) {
    std::vector<char> buf(64 * 1024);
    for (int i = 0; i < count; ++i) {
        char header[4];
        if (!ReadExactly(fd, header, 4)) {
            return i;
        }
        uint32_t len;
        memcpy(&len, header, 4);
        len = ntohl(len);
        if (len > buf.size()) {
            return i;
        }
        if (!ReadExactly(fd, buf.data(), len)) {
            return i;
        }
        // Every payload is its own index repeated, so a desync shows up immediately.
        std::string expected(len, static_cast<char>('a' + (i % 26)));
        if (std::string(buf.data(), len) != expected) {
            return i;
        }
    }
    return count;
}

void TestFramingIntact() {
    std::cout << "[1] framing survives a long run of messages" << std::endl;
    auto server = UnixSocketServer::Create(kPath);
    server->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = ConnectClient();
    Check(fd >= 0, "client connected");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int kCount = 500;
    std::atomic<int> received{0};
    std::thread reader([&] {
        received = DrainFrames(fd, kCount);
    });

    for (int i = 0; i < kCount; ++i) {
        // Sizes in the range a real InputReport occupies, 13..51 bytes.
        size_t size = 13 + (i % 39);
        server->Write(Frame(std::string(size, static_cast<char>('a' + (i % 26)))));
    }

    reader.join();
    Check(received == kCount, "all " + std::to_string(kCount) + " frames intact (got " +
                                  std::to_string(received.load()) + ")");

    close(fd);
    server->Stop();
}

void TestStalledConsumerDoesNotStarveHealthyOne() {
    std::cout << "[2] a consumer that stops reading does not stall a healthy one" << std::endl;
    auto server = UnixSocketServer::Create(kPath);
    server->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int stalled = ConnectClient(); // connects, then never reads a byte
    int healthy = ConnectClient();
    Check(stalled >= 0 && healthy >= 0, "both clients connected");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int kCount = 2000; // ~2 MB, far past any socket buffer
    std::atomic<int> received{0};
    std::thread reader([&] {
        received = DrainFrames(healthy, kCount);
    });

    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        server->Write(Frame(std::string(1024, static_cast<char>('a' + (i % 26)))));
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                       .count();

    reader.join();
    std::cout << "  ..    " << kCount << " writes took " << elapsed << " ms" << std::endl;
    Check(elapsed < 10000, "writes were not blocked indefinitely by the stalled client");
    Check(received == kCount, "healthy client got all " + std::to_string(kCount) +
                                  " frames intact (got " + std::to_string(received.load()) + ")");

    close(stalled);
    close(healthy);
    server->Stop();
}

} // namespace

int main() {
    TestFramingIntact();
    TestStalledConsumerDoesNotStarveHealthyOne();
    std::cout << (g_failures == 0 ? "\nALL PASSED" : "\nFAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
