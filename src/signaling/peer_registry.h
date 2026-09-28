#ifndef PEER_REGISTRY_H_
#define PEER_REGISTRY_H_

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/worker.h"
#include "rtc/rtc_peer.h"

// Keeps the peers a signaling service creates for its clients.
class PeerRegistry {
  public:
    using OnExpiredFunc = std::function<void(const std::string &peer_id)>;

    ~PeerRegistry();

    void OnExpired(OnExpiredFunc func) { on_expired_ = std::move(func); };

    void Start();
    void Stop();

    void Add(webrtc::scoped_refptr<RtcPeer> peer);
    webrtc::scoped_refptr<RtcPeer> Get(const std::string &peer_id);
    void Remove(const std::string &peer_id);

  private:
    struct Cleaner {
        std::mutex mutex;
        std::condition_variable cv;
        bool stopped = false;
        bool expiry_pending = false;

        void Wake();
    };

    void Sweep();

    OnExpiredFunc on_expired_ = nullptr;

    std::mutex mutex_;
    std::unordered_map<std::string, webrtc::scoped_refptr<RtcPeer>> peers_;

    std::shared_ptr<Cleaner> cleaner_ = std::make_shared<Cleaner>();
    std::unique_ptr<Worker> worker_;
};

#endif
