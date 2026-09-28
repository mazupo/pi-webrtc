#ifndef ENDPOINT_REGISTRY_H_
#define ENDPOINT_REGISTRY_H_

#include <map>
#include <memory>
#include <string>

#include "ipc/unix_socket_endpoint.h"

// The endpoints this device serves, keyed by their Ipc.endpoint name.
// The set is fixed at startup; payloads for any other name are dropped.
class EndpointRegistry {
  public:
    void Add(std::shared_ptr<UnixSocketEndpoint> endpoint);
    bool Write(const std::string &name, const std::string &remote_id,
               const std::string &payload) const;
    void OnRemoteClosed(const std::string &remote_id) const;
    void RegisterMessageCallback(const std::string &id,
                                 const UnixSocketServer::MessageCallback &callback) const;
    void UnregisterMessageCallback(const std::string &id) const;
    void StartAll();
    void StopAll();

  private:
    std::map<std::string, std::shared_ptr<UnixSocketEndpoint>> endpoints_;
};

#endif // ENDPOINT_REGISTRY_H_
