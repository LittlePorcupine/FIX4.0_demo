/**
 * @file server.hpp
 * @brief FIX 服务端实现
 *
 * 提供 FIX 协议服务端，支持多客户端连接、
 * 连接绑定线程模型和优雅关闭。
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>
#include <csignal>

namespace fix40 {

// 前置声明
class Reactor;
class ThreadPool;
class TimingWheel;
class Connection;
class Application;

/**
 * @class FixServer
 * @brief FIX 协议服务端
 *
 * 实现 FIX 协议服务端，主要功能：
 * - 监听 TCP 端口，接受客户端连接
 * - 为每个连接创建 Session 和 Connection
 * - 使用 Reactor 模式处理 I/O 事件
 * - 使用线程池处理业务逻辑
 * - 支持 SIGINT/SIGTERM 信号优雅关闭
 *
 * @par 线程模型
 * - Reactor 线程：负责 I/O 事件检测
 * - 工作线程池：负责消息处理，每个连接绑定到固定线程
 *
 * @par 使用示例
 * @code
 * FixServer server(9000, 4);  // 端口 9000，4 个工作线程
 * server.start();  // 阻塞直到收到停止信号
 * @endcode
 */
class FixServer {
public:
    /**
     * @brief 构造服务端
     * @param port 监听端口
     * @param num_threads 工作线程数（0 表示使用 CPU 核心数）
     * @param app 应用层处理器指针（可选，用于处理业务消息）
     * @throws std::runtime_error 创建 socket 或绑定失败时抛出
     */
    FixServer(int port, int num_threads, Application* app = nullptr);

    /**
     * @brief 析构函数
     *
     * 停止 Reactor，关闭监听 socket。
     */
    ~FixServer();

    /**
     * @brief 启动服务端
     *
     * 阻塞当前线程，运行事件循环直到收到停止信号。
     * 收到 SIGINT/SIGTERM 后执行优雅关闭流程。
     */
    void start();

private:
    /**
     * @brief 处理新连接
     * @param fd 新连接的 socket 文件描述符
     */
    void on_new_connection(int fd);

    /**
     * @brief 处理连接关闭
     * @param fd 关闭的连接文件描述符
     */
    void on_connection_close(int fd);

    /**
     * @brief 信号处理函数
     * @param signum 信号编号
     */
    static void signal_handler(int signum);

    const int port_;    ///< 监听端口
    int listen_fd_;     ///< 监听 socket 文件描述符

    std::unique_ptr<Reactor> reactor_;         ///< Reactor 事件循环
    std::unique_ptr<ThreadPool> worker_pool_;  ///< 工作线程池
    std::unique_ptr<TimingWheel> timing_wheel_; ///< 时间轮定时器

    /// 连接映射：fd -> Connection
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_; ///< 保护 connections_ 的互斥锁

    static volatile std::sig_atomic_t last_signal_; ///< 最近一次收到的信号编号（仅用于信号回调传递）
    static volatile std::sig_atomic_t signal_write_fd_; ///< self-pipe 写端 fd（仅用于 signal_handler）

    int signal_pipe_[2] = {-1, -1}; ///< self-pipe: [0]=read end, [1]=write end

    Application* application_ = nullptr;  ///< 应用层处理器指针
};

} // namespace fix40
