/**
 * @file connection.hpp
 * @brief TCP 连接管理类
 *
 * 封装单个 TCP 连接的 I/O 操作，实现连接绑定线程模型，
 * 确保同一连接的所有操作在同一线程串行执行。
 */

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
 * @class Connection
 * @brief TCP 连接管理类
 *
 * 负责管理单个客户端连接的 I/O 操作，包括：
 * - 非阻塞读取（ET 模式）
 * - 带缓冲的非阻塞写入
 * - FIX 消息帧解码
 * - 连接生命周期管理
 *
 * @par 线程模型
 * 每个连接绑定到一个固定的工作线程，所有操作（读、写、定时任务）
 * 都在该线程中串行执行，避免锁竞争。
 *
 * @par 数据流
 * @code
 * 接收: socket -> handle_read() -> FixFrameDecoder -> Session::on_message_received()
 * 发送: Session::send() -> Connection::send() -> do_send() -> socket
 * @endcode
 *
 * @note 该类继承 std::enable_shared_from_this，必须通过 std::shared_ptr 管理
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    /// 读缓冲区最大大小（1 MB）
    static constexpr size_t kMaxReadBufferSize = 1 * 1024 * 1024;

    /**
     * @brief 构造连接对象
     * @param fd 已连接的 socket 文件描述符（应已设置为非阻塞）
     * @param reactor Reactor 指针，用于注册/修改 I/O 事件
     * @param session 关联的 FIX 会话对象
     * @param thread_pool 线程池指针，用于派发任务
     * @param thread_index 绑定的工作线程索引
     */
    Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session,
               ThreadPool* thread_pool, size_t thread_index);

    /**
     * @brief 析构函数
     *
     * 自动关闭连接并释放资源。
     */
    ~Connection();

    /**
     * @brief 处理读事件
     *
     * 在 ET（边缘触发）模式下循环读取数据直到 EAGAIN，
     * 将数据送入帧解码器，解析出完整的 FIX 消息后交给 Session 处理。
     *
     * @note 必须在绑定的工作线程中调用
     *
     * @par 错误处理
     * - 缓冲区溢出：通知 Session I/O 错误
     * - 对端关闭：通知 Session 连接关闭
     * - 解码错误：通知 Session I/O 错误
     */
    void handle_read();

    /**
     * @brief 处理写事件
     *
     * 在 ET 模式下循环发送写缓冲区中的数据直到 EAGAIN 或发送完毕。
     * 发送完毕后取消写事件监听。
     *
     * @note 必须在绑定的工作线程中调用
     */
    void handle_write();
    
    /**
     * @brief 发送数据
     * @param data 要发送的数据
     *
     * 可以从任意线程调用，内部会将发送操作派发到绑定的工作线程执行。
     * 如果连接已关闭，调用无效。
     */
    void send(std::string_view data);

    /**
     * @brief 派发任务到绑定的工作线程执行
     * @param task 要执行的任务
     *
     * 确保任务在连接绑定的线程中串行执行，避免竞态条件。
     */
    void dispatch(std::function<void()> task);

    /**
     * @brief 关闭连接
     *
     * 从 Reactor 移除 fd，关闭 socket。
     * 该方法是幂等的，多次调用安全。
     */
    void shutdown();

    /**
     * @brief 关闭文件描述符
     *
     * 内部调用 shutdown()，保持接口一致性。
     */
    void close_fd();

    /**
     * @brief 获取 socket 文件描述符
     * @return int 文件描述符
     */
    int fd() const { return fd_; }

    /**
     * @brief 获取绑定的线程索引
     * @return size_t 线程索引
     */
    size_t thread_index() const { return thread_index_; }

    /**
     * @brief 获取关联的 Session 对象
     * @return std::shared_ptr<Session> Session 指针
     */
    std::shared_ptr<Session> session() const { return session_; }

private:
    /**
     * @brief 内部发送实现
     * @param data 要发送的数据
     *
     * 在绑定的工作线程中执行，尝试直接发送，
     * 发送不完则缓存并注册写事件。
     */
    void do_send(const std::string& data);

    const int fd_;                        ///< socket 文件描述符
    Reactor* reactor_;                    ///< Reactor 指针
    std::shared_ptr<Session> session_;    ///< 关联的 FIX 会话
    ThreadPool* thread_pool_;             ///< 线程池指针
    const size_t thread_index_;           ///< 绑定的工作线程索引
    std::atomic<bool> is_closed_{false};  ///< 连接关闭标志

    FixFrameDecoder frame_decoder_;       ///< FIX 消息帧解码器
    std::string write_buffer_;            ///< 写缓冲区
    // 注意：移除了 write_mutex_，因为同一连接的所有操作都在同一线程
};

} // namespace fix40
