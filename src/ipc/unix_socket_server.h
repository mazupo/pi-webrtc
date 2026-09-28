
#ifndef UNIX_SOCKET_SERVER_H_
#define UNIX_SOCKET_SERVER_H_

#include <atomic>
#include <cstdint>
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

    void RegisterMessageCallback(const std::string &id, MessageCallback callback);
    void UnregisterMessageCallback(const std::string &id);
    void Write(const std::string &message);
    void PublishLatest(const std::string &message);

    void Start();
    void Stop();

  private:
    struct Client {
        std::thread thread;
        uint64_t sent_version = 0;
    };

    int server_fd_;
    std::string socket_path_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
    std::unordered_map<int, Client> clients_;
    std::mutex mutex_;
    std::unordered_map<std::string, MessageCallback> message_callbacks_;
    std::string latest_;
    uint64_t latest_version_ = 0;

    void AcceptLoop();
    void HandleClient(int client_fd);
    bool WriteAll(int fd, const std::string &message);
    void SendToAll(const std::function<bool(int, Client &)> &send);
    bool SendLatest(int fd, Client &client);
    void FlushLatest();
};

#endif
