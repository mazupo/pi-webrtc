#include "ipc/endpoint_registry.h"

#include "common/logging.h"

void EndpointRegistry::Add(std::shared_ptr<UnixSocketEndpoint> endpoint) {
    if (!endpoint) {
        return;
    }
    endpoints_[endpoint->name()] = std::move(endpoint);
}

bool EndpointRegistry::Write(const std::string &name, const std::string &remote_id,
                             const std::string &payload) const {
    auto it = endpoints_.find(name);
    if (it == endpoints_.end()) {
        return false;
    }
    it->second->Write(remote_id, payload);
    return true;
}

void EndpointRegistry::OnRemoteClosed(const std::string &remote_id) const {
    for (const auto &[_, endpoint] : endpoints_) {
        endpoint->OnRemoteClosed(remote_id);
    }
}

void EndpointRegistry::RegisterMessageCallback(
    const std::string &id, const UnixSocketServer::MessageCallback &callback) const {
    for (const auto &[_, endpoint] : endpoints_) {
        endpoint->RegisterMessageCallback(id, callback);
    }
}

void EndpointRegistry::UnregisterMessageCallback(const std::string &id) const {
    for (const auto &[_, endpoint] : endpoints_) {
        endpoint->UnregisterMessageCallback(id);
    }
}

void EndpointRegistry::StartAll() {
    for (auto &[name, endpoint] : endpoints_) {
        endpoint->Start();
        DEBUG_PRINT("IPC endpoint '%s' listening", name.c_str());
    }
}

void EndpointRegistry::StopAll() {
    for (auto &[name, endpoint] : endpoints_) {
        endpoint->Stop();
    }
}
