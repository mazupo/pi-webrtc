#ifndef IPC_ENDPOINTS_H_
#define IPC_ENDPOINTS_H_

#include <map>
#include <memory>
#include <string>

#include "ipc/unix_socket_server.h"

// IPC endpoints served by the device, keyed by Ipc.endpoint.
// The set is fixed at startup; unknown names are dropped.
//
// Reserved names are defined in protocol/protos/packet.proto.
class IpcEndpoints {
  public:
    // Default endpoint for empty Ipc.endpoint and legacy Packet.raw.
    static constexpr const char *kDefault = "";
    static constexpr const char *kGamepad = "gamepad";

    struct Endpoint {
        std::shared_ptr<UnixSocketServer> server;
        bool length_prefixed = false; // Prefix each payload with a big-endian uint32 length.
        bool bidirectional = false;   // Relay data written to the socket back to the peer.
    };

    bool empty() const { return endpoints_.empty(); }

    void Add(const std::string &name, std::shared_ptr<UnixSocketServer> server,
             bool length_prefixed, bool bidirectional);
    const Endpoint *Find(const std::string &name) const;
    bool Write(const std::string &name, const std::string &payload) const;
    void StartAll();
    void StopAll();

  private:
    std::map<std::string, Endpoint> endpoints_;
};

#endif // IPC_ENDPOINTS_H_
