#include "ipc/unix_socket_server.h"

#include "config.h"

int main() {
    AppConfig args;

    auto server = UnixSocketServer::Create(args.socket_path);
    server->Start();
    std::this_thread::sleep_for(std::chrono::minutes(10));
    server->Stop();
    return 0;
}
