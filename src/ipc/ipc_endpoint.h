#ifndef IPC_ENDPOINT_H_
#define IPC_ENDPOINT_H_

#include <memory>
#include <string>

#include "ipc/unix_socket_endpoint.h"

// The --enable-ipc endpoint: opaque bytes, passed through as-is in both directions.
class IpcEndpoint : public UnixSocketEndpoint {
  public:
    // Empty Ipc.endpoint and legacy Packet.raw land here.
    static constexpr const char *kName = "";

    static std::shared_ptr<IpcEndpoint> Create(const std::string &socket_path);

    explicit IpcEndpoint(const std::string &socket_path);

    const char *name() const override { return kName; }
    void Start() override;
    void Stop() override;
    void Write(const std::string &remote_id, const std::string &payload) override;
    void RegisterMessageCallback(const std::string &id,
                                 UnixSocketServer::MessageCallback callback) override;
    void UnregisterMessageCallback(const std::string &id) override;

  private:
    std::shared_ptr<UnixSocketServer> server_;
};

#endif // IPC_ENDPOINT_H_
