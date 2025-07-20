#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>
#include <mutex>

namespace fix40 {

// 前置声明
class Session;
class Reactor;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    static constexpr size_t kMaxReadBufferSize = 1 * 1024 * 1024; // 1 兆字节

    Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session);
    ~Connection();

    void handle_read();
    void handle_write();
    void send(std::string_view data);

    // 启动轻量的关联关闭
    void shutdown();

    // 立即关闭文件描述符
    void close_fd();

    int fd() const { return fd_; }
    std::shared_ptr<Session> session() const { return session_; }

private:
    const int fd_;
    Reactor* reactor_; // 非拥有型指针
    std::shared_ptr<Session> session_;
    std::atomic<bool> is_closed_{false};

    std::string read_buffer_;
    std::string write_buffer_;
    std::mutex write_mutex_;
};

} // namespace fix40
