#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <functional>

namespace fix40 {

// Forward declarations
class Session;
class Reactor;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session);
    ~Connection();

    void handle_read();
    void handle_write();
    void send(std::string_view data);
    void shutdown();

    int fd() const { return fd_; }
    std::shared_ptr<Session> session() const { return session_; }

private:
    const int fd_;
    Reactor* reactor_; // Non-owning pointer
    std::shared_ptr<Session> session_;

    std::string read_buffer_;
};

} // namespace fix40 