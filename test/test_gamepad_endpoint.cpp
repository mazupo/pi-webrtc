// Build and run:
//   cmake -B build-test -DBUILD_TEST=gamepad && cmake --build build-test -t test-gamepad-endpoint
//   ./build-test/test-gamepad-endpoint
//
// Covers the snapshots GamepadEndpoint writes. Timing checks sit well clear of their
// thresholds, so scheduler jitter cannot flip them:
//   1. a client that connects first gets `{"gamepads":{}}`
//   2. values come out clean: shortest floats, NaN and range clamped, all 32 button bits
//   3. identities are escaped, and anything that is not gamepad input is dropped
//   4. every gamepad still sending shares one snapshot; a closed channel drops its row at once
//   5. a gamepad silent for 500 ms is dropped on the next event
//   6. the last gamepad to go leaves `{"gamepads":{}}`

#include "ipc/gamepad_endpoint.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "proto/input.pb.h"

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

const char *kPath = "/tmp/test-gamepad-endpoint.sock";
int g_failures = 0;

// The same CLOCK_MONOTONIC reading that device_monotonic_ns carries.
uint64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
        .count();
}

void Check(bool ok, const std::string &what) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) {
        ++g_failures;
    }
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

// The next snapshot line, or nothing if none arrives in time.
std::optional<std::string> NextLine(int fd, int timeout_ms = 500) {
    pollfd pfd{fd, POLLIN, 0};
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        return std::nullopt;
    }
    std::string line;
    char c;
    while (::read(fd, &c, 1) == 1 && c != '\n') {
        line.push_back(c);
    }
    return line;
}

std::optional<json> Next(int fd) {
    auto line = NextLine(fd);
    if (!line) {
        return std::nullopt;
    }
    return json::parse(*line);
}

std::string Report(uint64_t sequence, float left_x = 0.0f, uint32_t buttons = 0) {
    protocol::InputReport report;
    report.set_sequence(sequence);
    report.mutable_timestamp()->set_monotonic_ns(5000000 + sequence);
    auto *pad = report.mutable_gamepad();
    pad->set_left_x(left_x);
    pad->set_buttons(buttons);
    pad->set_standard_mapping(true);
    return report.SerializeAsString();
}

bool Has(const json &snapshot, const std::string &remote_id) {
    return snapshot["gamepads"].contains(remote_id);
}

} // namespace

int main() {
    unlink(kPath);
    auto started = NowNs();
    auto endpoint = GamepadEndpoint::Create(kPath);
    endpoint->Start();
    std::this_thread::sleep_for(100ms);

    std::cout << "[1] a client that connects first" << std::endl;
    int fd = ConnectClient();
    auto first = Next(fd);
    Check(first && (*first)["gamepads"].empty(), "gets an empty gamepads object");
    Check(first && (*first)["device_monotonic_ns"] >= started &&
              (*first)["device_monotonic_ns"] <= NowNs(),
          "stamped with the device clock");

    std::cout << "[2] values come out clean" << std::endl;
    {
        protocol::InputReport report;
        report.set_sequence(1);
        auto *pad = report.mutable_gamepad();
        pad->set_left_x(0.1f);
        pad->set_left_y(NAN);
        pad->set_right_x(2.5f);
        pad->set_left_trigger(-0.3f);
        pad->set_buttons(1u << 0 | 1u << 7 | 1u << 17);
        pad->set_standard_mapping(true);
        endpoint->Write("alice", report.SerializeAsString());
    }
    auto line = NextLine(fd);
    Check(line && line->find("\"left_x\":0.1,") != std::string::npos,
          "0.1f prints as 0.1, not 0.10000000149011612");
    auto values = json::parse(line.value_or("{}"));
    const auto &alice = values["gamepads"]["alice"];
    Check(alice["left_y"] == 0.0, "NaN becomes 0");
    Check(alice["right_x"] == 1.0 && alice["left_trigger"] == 0.0, "out-of-range values clamp");
    Check(alice["buttons"] == (1u << 0 | 1u << 7 | 1u << 17), "all 32 button bits survive");
    Check(alice["pressed"] == json::array({"a", "rt"}), "pressed names bits 0-16 only");
    Check(alice["sequence"] == 1 && alice["standard_mapping"] == true, "the rest is kept");

    std::cout << "[3] identities are escaped, non-gamepad input is dropped" << std::endl;
    const std::string quoted = "he said \"hi\"\n";
    endpoint->Write(quoted, Report(1));
    auto escaped = Next(fd);
    Check(escaped && Has(*escaped, quoted), "an identity with quotes round-trips");
    endpoint->OnRemoteClosed(quoted);
    Next(fd);

    endpoint->Write("alice", "\xff\xff\xff");
    endpoint->Write("alice", "");
    Check(!NextLine(fd, 200), "garbage and an empty report publish nothing");

    std::cout << "[4] gamepads share one snapshot; a closed channel drops its row" << std::endl;
    endpoint->Write("alice", Report(2));
    Next(fd);
    endpoint->Write("bob", Report(1));
    auto both = Next(fd);
    Check(both && Has(*both, "alice") && Has(*both, "bob"), "alice and bob in one snapshot");
    endpoint->OnRemoteClosed("alice");
    auto closed = Next(fd);
    Check(closed && !Has(*closed, "alice") && Has(*closed, "bob"), "alice gone at once");

    std::cout << "[5] a gamepad silent for 500 ms" << std::endl;
    // carol sends until 100 ms; after that bob's reports are the only events.
    auto t0 = Clock::now();
    auto at = [&](std::chrono::milliseconds offset, const std::string &remote_id, uint64_t seq) {
        std::this_thread::sleep_until(t0 + offset);
        endpoint->Write(remote_id, Report(seq));
        return Next(fd);
    };
    at(0ms, "carol", 10);
    at(50ms, "carol", 11);
    at(100ms, "carol", 12);
    auto snap = at(400ms, "bob", 30);
    Check(snap && Has(*snap, "carol"), "carol stays 300 ms after her last report");

    std::this_thread::sleep_until(t0 + 800ms);
    auto before = NowNs();
    endpoint->Write("bob", Report(31));
    auto after = NowNs();
    snap = Next(fd);
    Check(snap && !Has(*snap, "carol"), "carol is gone once 500 ms pass, on bob's next report");
    Check(snap && (*snap)["device_monotonic_ns"] >= before &&
              (*snap)["device_monotonic_ns"] <= after,
          "each snapshot carries the time it was built");

    std::cout << "[6] the last gamepad to go" << std::endl;
    endpoint->OnRemoteClosed("bob");
    auto empty = Next(fd);
    Check(empty && (*empty)["gamepads"].empty(), "leaves an empty gamepads object");

    close(fd);
    endpoint->Stop();

    std::cout << (g_failures == 0 ? "\nALL PASSED" : "\nFAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
