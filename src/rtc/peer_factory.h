#ifndef PEER_FACTORY_H_
#define PEER_FACTORY_H_

#include <optional>
#include <string>

#include "rtc/rtc_peer.h"

// Creates RtcPeers with the app's capabilities attached: tracks, data channels and IPC.
// Signaling services use it; Conductor in the composition layer implements it.
class PeerFactory {
  public:
    virtual ~PeerFactory() = default;

    // Returns nullptr when the peer connection cannot be created.
    virtual webrtc::scoped_refptr<RtcPeer> CreatePeerConnection(PeerConfig config) = 0;
    virtual void EnsureTracksAdded(webrtc::scoped_refptr<RtcPeer> peer) = 0;
    // The camera alias that serves the requested stream, or nullopt when none does.
    virtual std::optional<std::string> ResolveWebrtcAlias(const std::string &requested) const = 0;
};

#endif // PEER_FACTORY_H_
