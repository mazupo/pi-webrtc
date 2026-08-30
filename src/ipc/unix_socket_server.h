
#ifndef UNIX_SOCKET_SERVER_H_
#define UNIX_SOCKET_SERVER_H_

#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

class UnixSocketServer {
  public:
    using MessageCallback = std::function<void(const std::string &)>;

    static std::shared_ptr<UnixSocketServer> Create(const std::string &socket_path);

    UnixSocketServer(const std::string &socket_path);
    ~UnixSocketServer();

    void RegisterPeerCallback(const std::string &id, MessageCallback callback);
    void UnregisterPeerCallback(const std::string &id);
    void Write(const std::string &message);
    void Start();
    void Stop();

  private:
    int server_fd_;
    std::string socket_path_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::unordered_map<int, std::thread> client_threads_;
    std::mutex mutex_;
    std::unordered_map<std::string, MessageCallback> peer_callbacks_;

    void AcceptLoop();
    void HandleClient(int client_fd);
    bool WriteAll(int fd, const std::string &message);
};

#endif
