/**
 * @file session.hpp
 * @brief FIX 会话层实现
 *
 * 实现 FIX 协议的会话层状态机，管理会话生命周期、
 * 心跳检测、消息序列号等。
 */

#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>

#include "fix/fix_codec.hpp"
#include "base/concurrentqueue.h"
#include "base/timing_wheel.hpp"

namespace fix40 {

class Connection;
class Session;

/**
 * @class IStateHandler
 * @brief 会话状态处理器接口（状态模式）
 *
 * 定义会话在不同状态下的行为接口。
 * 具体状态类（DisconnectedState、LogonSentState、EstablishedState、LogoutSentState）
 * 实现此接口以处理各状态下的事件。
 */
class IStateHandler {
public:
    virtual ~IStateHandler() = default;

    /**
     * @brief 处理收到的消息
     * @param context 会话上下文
     * @param msg 收到的 FIX 消息
     */
    virtual void onMessageReceived(Session& context, const FixMessage& msg) = 0;

    /**
     * @brief 处理定时器检查事件
     * @param context 会话上下文
     *
     * 用于心跳发送、超时检测等周期性任务。
     */
    virtual void onTimerCheck(Session& context) = 0;

    /**
     * @brief 处理会话启动事件
     * @param context 会话上下文
     */
    virtual void onSessionStart(Session& context) = 0;

    /**
     * @brief 处理登出请求
     * @param context 会话上下文
     * @param reason 登出原因
     */
    virtual void onLogoutRequest(Session& context, const std::string& reason) = 0;

    /**
     * @brief 获取状态名称
     * @return const char* 状态名称字符串
     */
    virtual const char* getStateName() const = 0;
};


/**
 * @class Session
 * @brief FIX 会话管理器
 *
 * 实现 FIX 协议的会话层，管理：
 * - 会话状态机（Disconnected -> LogonSent -> Established -> LogoutSent）
 * - 消息序列号
 * - 心跳检测和 TestRequest
 * - 优雅登出流程
 *
 * @par 状态转换图
 * @code
 *                     ┌──────────────┐
 *                     │ Disconnected │
 *                     └──────┬───────┘
 *                            │ start() [客户端发送 Logon]
 *                            ▼
 *                     ┌──────────────┐
 *                     │  LogonSent   │
 *                     └──────┬───────┘
 *                            │ 收到 Logon 确认
 *                            ▼
 *                     ┌──────────────┐
 *                     │ Established  │
 *                     └──────┬───────┘
 *                            │ 发起 Logout
 *                            ▼
 *                     ┌──────────────┐
 *                     │  LogoutSent  │
 *                     └──────┬───────┘
 *                            │ 收到 Logout 确认
 *                            ▼
 *                     ┌──────────────┐
 *                     │ Disconnected │
 *                     └──────────────┘
 * @endcode
 *
 * @note 该类继承 std::enable_shared_from_this，必须通过 std::shared_ptr 管理
 */
class Session : public std::enable_shared_from_this<Session> {
public:
    /// 会话关闭回调类型
    using ShutdownCallback = std::function<void()>;

    /**
     * @brief 构造会话对象
     * @param sender 发送方 CompID
     * @param target 接收方 CompID
     * @param hb 心跳间隔（秒）
     * @param shutdown_cb 会话关闭时的回调函数
     */
    Session(const std::string& sender,
            const std::string& target,
            int hb,
            ShutdownCallback shutdown_cb);

    /**
     * @brief 析构函数
     *
     * 取消定时任务，释放资源。
     */
    ~Session();

    /**
     * @brief 关联 Connection 对象
     * @param conn Connection 的弱引用
     */
    void set_connection(std::weak_ptr<Connection> conn);

    /**
     * @brief 启动会话
     *
     * 客户端：发送 Logon 消息
     * 服务端：等待客户端 Logon
     */
    void start();

    /**
     * @brief 停止会话
     */
    void stop();

    /**
     * @brief 检查会话是否正在运行
     * @return true 正在运行
     * @return false 已停止
     */
    bool is_running() const { return running_; }

    /**
     * @brief 发送 FIX 消息
     * @param msg 要发送的消息（会自动设置序列号）
     */
    void send(FixMessage& msg);

    /**
     * @brief 发送缓冲区中的数据
     */
    void send_buffered_data();

    /**
     * @brief 处理写就绪事件
     */
    void handle_write_ready();

    /**
     * @brief 将原始消息加入发送队列
     * @param raw_msg 原始消息字符串
     */
    void enqueue_raw_for_send(std::string&& raw_msg);

    /**
     * @brief 检查发送队列是否为空
     * @return true 队列为空
     * @return false 队列非空
     */
    bool is_outbound_queue_empty() const;

    /**
     * @brief 处理收到的消息
     * @param msg 解码后的 FIX 消息
     */
    void on_message_received(const FixMessage& msg);

    /**
     * @brief 处理定时器检查
     *
     * 由时间轮周期性调用，用于心跳发送和超时检测。
     */
    void on_timer_check();

    /**
     * @brief 处理 I/O 错误
     * @param reason 错误原因
     */
    void on_io_error(const std::string& reason);

    /**
     * @brief 处理连接关闭
     * @param reason 关闭原因
     */
    void on_shutdown(const std::string& reason);

    /**
     * @brief 发起优雅登出流程
     * @param reason 登出原因
     */
    void initiate_logout(const std::string& reason);

    /**
     * @brief 调度周期性定时任务
     * @param wheel 时间轮指针
     */
    void schedule_timer_tasks(TimingWheel* wheel);

    /**
     * @brief 切换会话状态
     * @param newState 新状态对象
     */
    void changeState(std::unique_ptr<IStateHandler> newState);

    const std::string senderCompID;  ///< 发送方 CompID
    const std::string targetCompID;  ///< 接收方 CompID
    FixCodec codec_;                 ///< FIX 编解码器

    // --- 公共辅助函数（供状态类调用）---

    /**
     * @brief 发送 Logout 消息
     * @param reason 登出原因
     */
    void send_logout(const std::string& reason);

    /**
     * @brief 发送 Heartbeat 消息
     * @param test_req_id TestReqID（响应 TestRequest 时填写）
     */
    void send_heartbeat(const std::string& test_req_id = "");

    /**
     * @brief 发送 TestRequest 消息
     * @param id 测试请求标识符
     */
    void send_test_request(const std::string& id);

    /**
     * @brief 执行会话关闭
     * @param reason 关闭原因
     */
    void perform_shutdown(const std::string& reason);

    /** @brief 更新最后接收时间 */
    void update_last_recv_time();

    /** @brief 更新最后发送时间 */
    void update_last_send_time();

    /** @brief 获取最后接收时间 */
    std::chrono::steady_clock::time_point get_last_recv_time() const;

    /** @brief 获取最后发送时间 */
    std::chrono::steady_clock::time_point get_last_send_time() const;

    /** @brief 获取心跳间隔（秒） */
    int get_heart_bt_int() const;

    /** @brief 设置心跳间隔 */
    void set_heart_bt_int(int new_hb);

    /** @brief 获取最小允许心跳间隔 */
    int get_min_heart_bt_int() const;

    /** @brief 获取最大允许心跳间隔 */
    int get_max_heart_bt_int() const;

    // --- 序列号管理 ---

    /** @brief 获取发送序列号 */
    int get_send_seq_num() { return sendSeqNum; }

    /** @brief 获取接收序列号 */
    int get_recv_seq_num() { return recvSeqNum; }

    /** @brief 递增发送序列号 */
    void increment_send_seq_num() { sendSeqNum++; }

    /** @brief 递增接收序列号 */
    void increment_recv_seq_num() { recvSeqNum++; }

    /** @brief 设置接收序列号 */
    void set_recv_seq_num(int seq) { recvSeqNum = seq; }

private:
    std::atomic<bool> shutting_down_{false};   ///< 关闭中标志
    std::recursive_mutex state_mutex_;          ///< 状态保护锁
    std::unique_ptr<IStateHandler> currentState_; ///< 当前状态对象

    /**
     * @brief 内部发送实现
     * @param raw_msg 原始消息字符串
     */
    void internal_send(const std::string& raw_msg);

    int heartBtInt;                  ///< 心跳间隔（秒）
    const int minHeartBtInt_;        ///< 最小心跳间隔
    const int maxHeartBtInt_;        ///< 最大心跳间隔
    ShutdownCallback shutdown_callback_; ///< 关闭回调
    std::weak_ptr<Connection> connection_; ///< 关联的连接

    moodycamel::ConcurrentQueue<std::string> outbound_q_; ///< 发送队列

    std::atomic<bool> running_{false}; ///< 运行状态

    int sendSeqNum = 1;  ///< 发送序列号
    int recvSeqNum = 1;  ///< 期望接收的序列号
    std::chrono::steady_clock::time_point lastRecv; ///< 最后接收时间
    std::chrono::steady_clock::time_point lastSend; ///< 最后发送时间

    TimingWheel* timing_wheel_ = nullptr;  ///< 时间轮指针
    TimerTaskId timer_task_id_ = INVALID_TIMER_ID; ///< 定时任务 ID
};

} // namespace fix40
