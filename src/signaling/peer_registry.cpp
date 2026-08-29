#include "signaling/peer_registry.h"

#include <chrono>
#include <vector>

#include "common/logging.h"

PeerRegistry::~PeerRegistry() { Stop(); }

void PeerRegistry::Start() {
    if (worker_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(cleaner_mutex_);
        stopped_ = false;
    }

    worker_ = std::make_unique<Worker>("cleaner", [this]() {
        std::unique_lock<std::mutex> lock(cleaner_mutex_);

        cleaner_cv_.wait_for(lock, std::chrono::seconds(kCleanupIntervalSec), [this] {
            return stopped_;
        });

        if (stopped_) {
            return;
        }

        lock.unlock();

        Sweep();
    });
    worker_->Run();
}

void PeerRegistry::Stop() {
    {
        std::lock_guard<std::mutex> lock(cleaner_mutex_);
        stopped_ = true;
    }
    cleaner_cv_.notify_all();
    worker_.reset();

    std::unordered_map<std::string, webrtc::scoped_refptr<RtcPeer>> peers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        peers.swap(peers_);
    }

    for (auto &[peer_id, peer] : peers) {
        if (peer) {
            peer->Terminate();
        }
    }
}

void PeerRegistry::Add(webrtc::scoped_refptr<RtcPeer> peer) {
    if (!peer) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    peers_[peer->id()] = std::move(peer);
}

webrtc::scoped_refptr<RtcPeer> PeerRegistry::Get(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        return it->second;
    }
    return nullptr;
}

void PeerRegistry::Remove(const std::string &peer_id) {
    webrtc::scoped_refptr<RtcPeer> peer;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = peers_.find(peer_id);
        if (it != peers_.end()) {
            peer = std::move(it->second);
            peers_.erase(it);
        }
    }

    if (peer) {
        peer->Terminate();
    }
}

void PeerRegistry::Sweep() {
    std::vector<std::string> expired_ids;
    std::vector<webrtc::scoped_refptr<RtcPeer>> expired;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = peers_.begin();
        while (it != peers_.end()) {
            const auto &peer_id = it->first;
            auto &peer = it->second;

            if (!peer) {
                DEBUG_PRINT("Peer %s exists in map but holds no peer!", peer_id.c_str());
                it = peers_.erase(it);
                continue;
            }

            const char *status = peer->is_expired()
                                     ? "expired"
                                     : (peer->is_connected() ? "connected" : "reconnecting");
            DEBUG_PRINT("Found peer_id key: %s, status: %s", peer_id.c_str(), status);

            if (peer->is_expired()) {
                DEBUG_PRINT("(%s) was erased.", peer_id.c_str());
                expired_ids.push_back(peer_id);
                expired.push_back(std::move(peer));
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto &peer : expired) {
        peer->Terminate();
    }

    if (!on_expired_) {
        return;
    }

    for (const auto &peer_id : expired_ids) {
        on_expired_(peer_id);
    }
}
