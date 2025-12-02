#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>
#include "fix/fix_frame_decoder.hpp"

namespace fix40 {

// 前置声明
class Session;
class Reactor;
class ThreadPool;

/**
 * 连接类，负责管理单个客户端连接的 IO 操作
 * 
 * 每个连接绑定到一个固定的工作线程，所有操作（读、写、定时任务）
 * 都在该线程中串行执行，避免锁竞争。
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    static constexpr size_t kMaxReadBufferSize = 1 * 1024 * 1024; // 1 MB

    Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session,
               ThreadPool* thread_pool, size_t thread_index);
    ~Connection();

    // IO 操作（必须在绑定的线程中调用）
    void handle_read();
    void handle_write();
    
    // 发送数据（可以从任意线程调用，内部会派发到绑定线程）
    void send(std::string_view data);

    // 派发任务到绑定的线程执行
    void dispatch(std::function<void()> task);

    // 关闭连接
    void shutdown();
    void close_fd();

    int fd() const { return fd_; }
    size_t thread_index() const { return thread_index_; }
    std::shared_ptr<Session> session() const { return session_; }

private:
    // 内部发送实现（在绑定线程中执行）
    void do_send(const std::string& data);

    const int fd_;
    Reactor* reactor_;
    std::shared_ptr<Session> session_;
    ThreadPool* thread_pool_;
    const size_t thread_index_;  // 绑定的线程索引
    std::atomic<bool> is_closed_{false};

    FixFrameDecoder frame_decoder_;
    std::string write_buffer_;
    // 注意：移除了 write_mutex_，因为同一连接的所有操作都在同一线程
};

} // namespace fix40
