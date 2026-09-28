#include "ipc/unix_socket_server.h"

#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <vector>

#include "common/logging.h"

std::shared_ptr<UnixSocketServer> UnixSocketServer::Create(const std::string &socket_path) {
    return std::make_shared<UnixSocketServer>(socket_path);
}

UnixSocketServer::UnixSocketServer(const std::string &socket_path)
    : server_fd_(-1),
      socket_path_(socket_path),
      running_(false) {}

UnixSocketServer::~UnixSocketServer() { Stop(); }

void UnixSocketServer::RegisterMessageCallback(const std::string &id, MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    message_callbacks_[id] = std::move(callback);
}

void UnixSocketServer::UnregisterMessageCallback(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    message_callbacks_.erase(id);
}

bool UnixSocketServer::WriteAll(int fd, const std::string &message) {
    size_t sent = 0;
    while (sent < message.size()) {
        // Prevent SIGPIPE when the client disconnects.
        ssize_t n = ::send(fd, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ERROR_PRINT("Failed to write to client fd=%d after %zu of %zu bytes: %s", fd, sent,
                    message.size(), strerror(errno));
        return false;
    }
    return true;
}

void UnixSocketServer::Write(const std::string &message) {
    SendToAll([this, &message](int fd, Client &) {
        return WriteAll(fd, message);
    });
}

void UnixSocketServer::SendToAll(const std::function<bool(int, Client &)> &send) {
    std::vector<int> stale;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (send(it->first, it->second)) {
                ++it;
                continue;
            }

            stale.push_back(it->first);
            if (it->second.thread.joinable()) {
                it->second.thread.detach();
            }
            it = clients_.erase(it);
        }
    }

    for (int fd : stale) {
        shutdown(fd, SHUT_RDWR);
    }
}

bool UnixSocketServer::SendLatest(int fd, Client &client) {
    if (latest_version_ == 0 || client.sent_version == latest_version_) {
        return true;
    }

    // Still holding the previous message unread: the next publish tries again.
    int unread = 0;
    if (ioctl(fd, SIOCOUTQ, &unread) < 0) {
        return false;
    }
    if (unread > 0) {
        return true;
    }

    // MSG_DONTWAIT leaves the fd blocking, so HandleClient's read() is unaffected.
    ssize_t n = ::send(fd, latest_.data(), latest_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n == static_cast<ssize_t>(latest_.size())) {
        client.sent_version = latest_version_;
        return true;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return true;
    }

    if (n < 0) {
        DEBUG_PRINT("Dropping client fd=%d: %s", fd, strerror(errno));
    } else {
        // A short write would leave half a message behind.
        ERROR_PRINT("Dropping client fd=%d after writing %zd of %zu bytes", fd, n, latest_.size());
    }
    return false;
}

void UnixSocketServer::FlushLatest() {
    SendToAll([this](int fd, Client &client) {
        return SendLatest(fd, client);
    });
}

void UnixSocketServer::PublishLatest(const std::string &message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = message;
        ++latest_version_;
    }
    FlushLatest();
}

void UnixSocketServer::Start() {
    sockaddr_un addr;
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        perror("socket");
        return;
    }

    unlink(socket_path_.c_str());
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd_);
        return;
    }

    if (listen(server_fd_, 16) == -1) {
        perror("listen");
        close(server_fd_);
        return;
    }

    running_ = true;
    accept_thread_ = std::thread(&UnixSocketServer::AcceptLoop, this);
}

void UnixSocketServer::Stop() {
    running_ = false;

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable())
        accept_thread_.join();

    std::unordered_map<int, Client> local_clients;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &[fd, _] : clients_) {
            shutdown(fd, SHUT_RDWR);
        }

        local_clients.swap(clients_);
    }

    for (auto &[fd, client] : local_clients) {
        if (client.thread.joinable())
            client.thread.join(); // already detached ones won't be in here
        close(fd);
    }

    unlink(socket_path_.c_str());
}

void UnixSocketServer::AcceptLoop() {
    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_) {
                perror("accept");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        // Drop clients that block writes for too long.
        timeval send_timeout{};
        send_timeout.tv_usec = 200 * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_[client_fd].thread =
                std::thread(&UnixSocketServer::HandleClient, this, client_fd);
        }

        FlushLatest();
    }
}

void UnixSocketServer::HandleClient(int client_fd) {
    char buffer[1024];
    while (running_) {
        int n = read(client_fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }

        std::string msg(buffer, n);
        DEBUG_PRINT("[%d] Received: %s", client_fd, msg.c_str());

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &[_, callback] : message_callbacks_) {
            if (callback) {
                callback(msg);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(client_fd);
        if (it != clients_.end()) {
            if (std::this_thread::get_id() == it->second.thread.get_id()) {
                it->second.thread.detach();
            }
            clients_.erase(it);
        }
    }

    close(client_fd);

    DEBUG_PRINT("[%d] leaved!", client_fd);
}
