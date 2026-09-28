#ifndef UNIX_SOCKET_ENDPOINT_H_
#define UNIX_SOCKET_ENDPOINT_H_

#include <string>

#include "ipc/unix_socket_server.h"

// A named destination for IPC payloads from peers, served on a unix socket.
class UnixSocketEndpoint {
  public:
    virtual ~UnixSocketEndpoint() = default;

    // The Ipc.endpoint name served; reserved names are listed in protocol/protos/packet.proto.
    virtual const char *name() const = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Write(const std::string &remote_id, const std::string &payload) = 0;
    virtual void OnRemoteClosed(const std::string &remote_id) {}
    // Socket client writes are passed to every registered callback; one-way endpoints ignore this.
    virtual void RegisterMessageCallback(const std::string &id,
                                         UnixSocketServer::MessageCallback callback) {}
    virtual void UnregisterMessageCallback(const std::string &id) {}
};

#endif // UNIX_SOCKET_ENDPOINT_H_
