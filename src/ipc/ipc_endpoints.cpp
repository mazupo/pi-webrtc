#include "ipc/ipc_endpoints.h"

#include <arpa/inet.h>

#include "common/logging.h"

void IpcEndpoints::Add(const std::string &name, std::shared_ptr<UnixSocketServer> server,
                       bool length_prefixed, bool bidirectional) {
    if (!server) {
        return;
    }
    endpoints_[name] = Endpoint{std::move(server), length_prefixed, bidirectional};
}

const IpcEndpoints::Endpoint *IpcEndpoints::Find(const std::string &name) const {
    auto it = endpoints_.find(name);
    return it == endpoints_.end() ? nullptr : &it->second;
}

bool IpcEndpoints::Write(const std::string &name, const std::string &payload) const {
    const auto *endpoint = Find(name);
    if (!endpoint) {
        return false;
    }

    if (!endpoint->length_prefixed) {
        endpoint->server->Write(payload);
        return true;
    }

    // Big-endian, so the length reads the same way on either side without either having to know the
    // other's byte order.
    uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    std::string framed(reinterpret_cast<const char *>(&length), sizeof(length));
    framed += payload;
    endpoint->server->Write(framed);
    return true;
}

void IpcEndpoints::StartAll() {
    for (auto &[name, endpoint] : endpoints_) {
        endpoint.server->Start();
        DEBUG_PRINT("IPC endpoint '%s' listening", name.c_str());
    }
}

void IpcEndpoints::StopAll() {
    for (auto &[name, endpoint] : endpoints_) {
        endpoint.server->Stop();
    }
}
