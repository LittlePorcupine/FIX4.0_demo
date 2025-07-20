#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>

namespace fix40 {

// Forward declarations
class Session;
class Reactor;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    static constexpr size_t kMaxReadBufferSize = 1 * 1024 * 1024; // 1 MB

    Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session);
    ~Connection();

    void handle_read();
    void handle_write();
    void send(std::string_view data);

    // Initiates a graceful shutdown of the connection.
    void shutdown();

    // Immediately closes the file descriptor.
    void close_fd();

    int fd() const { return fd_; }
    std::shared_ptr<Session> session() const { return session_; }

private:
    const int fd_;
    Reactor* reactor_; // Non-owning pointer
    std::shared_ptr<Session> session_;
    std::atomic<bool> is_closed_{false};

    std::string read_buffer_;
};

} // namespace fix40