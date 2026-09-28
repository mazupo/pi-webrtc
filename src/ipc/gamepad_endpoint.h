#ifndef GAMEPAD_ENDPOINT_H_
#define GAMEPAD_ENDPOINT_H_

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "ipc/unix_socket_endpoint.h"

// Serves browser gamepad input as one JSON snapshot per line: every gamepad still sending,
// keyed by remote_id, stamped with the device's CLOCK_MONOTONIC. A client holds at most one
// unread snapshot.
//
// A gamepad is dropped when its peer is torn down, or once it sends nothing for 500 ms. That
// check runs on the next event; no timer runs.
class GamepadEndpoint : public UnixSocketEndpoint {
  public:
    static constexpr const char *kName = "gamepad";

    static std::shared_ptr<GamepadEndpoint> Create(const std::string &socket_path);

    explicit GamepadEndpoint(const std::string &socket_path);

    const char *name() const override { return kName; }
    void Start() override;
    void Stop() override;
    void Write(const std::string &remote_id, const std::string &payload) override;
    void OnRemoteClosed(const std::string &remote_id) override;

  private:
    using Clock = std::chrono::steady_clock;

    struct Gamepad {
        nlohmann::ordered_json state;
        Clock::time_point last_received;
    };

    void PruneLocked(Clock::time_point now);
    void PublishLocked(Clock::time_point now);

    std::shared_ptr<UnixSocketServer> server_;

    std::mutex mutex_;
    std::map<std::string, Gamepad> gamepads_;
};

#endif // GAMEPAD_ENDPOINT_H_
