#include "ipc/ipc_endpoint.h"

std::shared_ptr<IpcEndpoint> IpcEndpoint::Create(const std::string &socket_path) {
    return std::make_shared<IpcEndpoint>(socket_path);
}

IpcEndpoint::IpcEndpoint(const std::string &socket_path)
    : server_(UnixSocketServer::Create(socket_path)) {}

void IpcEndpoint::Start() { server_->Start(); }

void IpcEndpoint::Stop() { server_->Stop(); }

void IpcEndpoint::Write(const std::string &remote_id, const std::string &payload) {
    server_->Write(payload);
}

void IpcEndpoint::RegisterMessageCallback(const std::string &id,
                                          UnixSocketServer::MessageCallback callback) {
    server_->RegisterMessageCallback(id, std::move(callback));
}

void IpcEndpoint::UnregisterMessageCallback(const std::string &id) {
    server_->UnregisterMessageCallback(id);
}
