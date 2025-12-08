/**
 * @file client.hpp
 * @brief FIX 客户端实现
 *
 * 提供 FIX 协议客户端，支持连接服务器、
 * 自动登录和控制台交互。
 */

#pragma once

#include <string>
#include <memory>
#include <thread>

namespace fix40 {

class Connection;
class Session;
class Reactor;
class ThreadPool;
class TimingWheel;

/**
 * @class Client
 * @brief FIX 协议客户端
 *
 * 实现 FIX 协议客户端，主要功能：
 * - 连接到 FIX 服务器
 * - 自动发送 Logon 消息建立会话
 * - 维护心跳
 * - 支持控制台交互（输入 "logout" 断开）
 *
 * @par 线程模型
 * - 主线程：控制台输入处理
 * - Reactor 线程：I/O 事件处理
 * - 工作线程：消息处理
 *
 * @par 使用示例
 * @code
 * Client client;
 * if (client.connect("127.0.0.1", 9000)) {
 *     client.run_console();  // 阻塞直到用户输入 logout
 * }
 * @endcode
 */
class Client {
public:
    /**
     * @brief 构造客户端
     *
     * 初始化 Reactor、线程池和时间轮。
     */
    Client();

    /**
     * @brief 析构函数
     *
     * 停止 Reactor，等待 Reactor 线程结束。
     */
    ~Client();

    /**
     * @brief 连接到服务器
     * @param ip 服务器 IP 地址
     * @param port 服务器端口
     * @return true 连接成功
     * @return false 连接失败
     *
     * 连接成功后自动发送 Logon 消息。
     */
    bool connect(const std::string& ip, int port);

    /**
     * @brief 断开连接
     *
     * 发起优雅登出流程，发送 Logout 消息并等待确认。
     */
    void disconnect();

    /**
     * @brief 运行控制台交互
     *
     * 阻塞当前线程，等待用户输入。
     * 输入 "logout" 触发断开连接。
     */
    void run_console();

private:
    /**
     * @brief 处理连接关闭事件
     */
    void on_connection_close();

    std::unique_ptr<Reactor> reactor_;         ///< Reactor 事件循环
    std::unique_ptr<ThreadPool> worker_pool_;  ///< 工作线程池
    std::unique_ptr<TimingWheel> timing_wheel_; ///< 时间轮定时器

    std::shared_ptr<Connection> connection_;   ///< 连接对象
    std::shared_ptr<Session> session_;         ///< 会话对象

    std::thread reactor_thread_;               ///< Reactor 线程
};

} // namespace fix40
