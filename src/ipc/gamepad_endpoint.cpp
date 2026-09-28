#include "ipc/gamepad_endpoint.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>

#include "proto/input.pb.h"

#include "common/logging.h"

namespace {

using namespace std::chrono_literals;

// A gamepad silent this long counts as gone.
constexpr auto kTimeout = 500ms;

// buttons[N].pressed is bit N, in the W3C "standard" mapping.
constexpr const char *kButtonNames[] = {
    "a",     "b",  "x",  "y",       "lb",        "rb",        "lt",         "rt",    "back",
    "start", "l3", "r3", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "guide",
};

// Clamps to [lo, hi], with NaN, inf and -0 as 0, and returns the shortest decimal that
// round-trips the float, so 0.1f prints as 0.1 rather than 0.10000000149011612.
double ClampForJson(float value, float lo, float hi) {
    if (!std::isfinite(value) || value == 0.0f) {
        value = 0.0f;
    }
    value = std::clamp(value, lo, hi);

    char buf[32];
    auto result = std::to_chars(buf, buf + sizeof(buf), value);
    double out = 0.0;
    std::from_chars(buf, result.ptr, out);
    return out;
}

nlohmann::ordered_json ToJson(const protocol::InputReport &report) {
    const auto &pad = report.gamepad();

    nlohmann::ordered_json state;
    state["sequence"] = report.sequence();
    if (report.has_timestamp()) {
        auto &timestamp = state["timestamp"];
        timestamp["monotonic_ns"] = report.timestamp().monotonic_ns();
        if (report.timestamp().has_unix_ns()) {
            timestamp["unix_ns"] = report.timestamp().unix_ns();
        }
    }
    state["left_x"] = ClampForJson(pad.left_x(), -1.0f, 1.0f);
    state["left_y"] = ClampForJson(pad.left_y(), -1.0f, 1.0f);
    state["right_x"] = ClampForJson(pad.right_x(), -1.0f, 1.0f);
    state["right_y"] = ClampForJson(pad.right_y(), -1.0f, 1.0f);
    state["left_trigger"] = ClampForJson(pad.left_trigger(), 0.0f, 1.0f);
    state["right_trigger"] = ClampForJson(pad.right_trigger(), 0.0f, 1.0f);
    state["buttons"] = pad.buttons();

    auto &pressed = state["pressed"] = nlohmann::ordered_json::array();
    for (size_t bit = 0; bit < std::size(kButtonNames); ++bit) {
        if (pad.buttons() >> bit & 1u) {
            pressed.push_back(kButtonNames[bit]);
        }
    }
    state["standard_mapping"] = pad.standard_mapping();
    return state;
}

} // namespace

std::shared_ptr<GamepadEndpoint> GamepadEndpoint::Create(const std::string &socket_path) {
    return std::make_shared<GamepadEndpoint>(socket_path);
}

GamepadEndpoint::GamepadEndpoint(const std::string &socket_path)
    : server_(UnixSocketServer::Create(socket_path)) {}

void GamepadEndpoint::Start() {
    {
        // Clients that connect before any input still get `{"gamepads":{}}`.
        std::lock_guard<std::mutex> lock(mutex_);
        PublishLocked(Clock::now());
    }
    server_->Start();
}

void GamepadEndpoint::Stop() { server_->Stop(); }

void GamepadEndpoint::Write(const std::string &remote_id, const std::string &payload) {
    protocol::InputReport report;
    if (!report.ParseFromString(payload) || !report.has_gamepad()) {
        DEBUG_PRINT("Dropping %zu bytes of non-gamepad input from '%s'", payload.size(),
                    remote_id.c_str());
        return;
    }

    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto &gamepad = gamepads_[remote_id];
    gamepad.last_received = now;
    gamepad.state = ToJson(report);

    PruneLocked(now);
    PublishLocked(now);
}

void GamepadEndpoint::OnRemoteClosed(const std::string &remote_id) {
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    if (gamepads_.erase(remote_id) == 0) {
        return;
    }
    DEBUG_PRINT("Gamepad '%s' closed", remote_id.c_str());
    PruneLocked(now);
    PublishLocked(now);
}

void GamepadEndpoint::PruneLocked(Clock::time_point now) {
    for (auto it = gamepads_.begin(); it != gamepads_.end();) {
        if (now - it->second.last_received > kTimeout) {
            DEBUG_PRINT("Gamepad '%s' went silent", it->first.c_str());
            it = gamepads_.erase(it);
        } else {
            ++it;
        }
    }
}

void GamepadEndpoint::PublishLocked(Clock::time_point now) {
    nlohmann::ordered_json snapshot;
    snapshot["device_monotonic_ns"] = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    auto &pads = snapshot["gamepads"] = nlohmann::ordered_json::object();
    for (const auto &[remote_id, gamepad] : gamepads_) {
        pads[remote_id] = gamepad.state;
    }

    // Replace, not throw, on an identity that is not valid UTF-8.
    auto line = snapshot.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    server_->PublishLatest(line + "\n");
}
