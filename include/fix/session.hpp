#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory> // 以便使用 weak_ptr

#include "fix/fix_codec.hpp"
// #include "fix_events.hpp" // 2. 不再需要 events
#include "base/concurrentqueue.h" // <--- 改为使用 moodycamel
#include "base/timing_wheel.hpp"

namespace fix40 {

class Connection;
class Session; // 前向声明

// 状态模式接口
class IStateHandler {
public:
    virtual ~IStateHandler() = default;

    // 当收到消息时调用
    virtual void onMessageReceived(Session& context, const FixMessage& msg) = 0;
    // 当定时器触发时调用
    virtual void onTimerCheck(Session& context) = 0;
    // 当会话启动时调用
    virtual void onSessionStart(Session& context) = 0;
    // 当外部请求登出时调用
    virtual void onLogoutRequest(Session& context, const std::string& reason) = 0;
    // 获取状态名称（用于调试）
    virtual const char* getStateName() const = 0;
};


class Session : public std::enable_shared_from_this<Session> {
public:
    using ShutdownCallback = std::function<void()>;

    Session(const std::string& sender,
            const std::string& target,
            int hb,
            ShutdownCallback shutdown_cb);

    ~Session();

    // 新方法：将 Session 和 Connection 关联
    void set_connection(std::weak_ptr<Connection> conn);

    void start();
    void stop();

    bool is_running() const { return running_; }

    // 公共的发送接口: 编码并尝试直接发送
    void send(FixMessage& msg);
    // 新增: 用于处理写事件，发送缓冲区中的数据
    void send_buffered_data();
    // 新增: 用于处理写事件，发送缓冲区中的数据
    void handle_write_ready();

    // 新增: 为 Connection 提供与 outbound_q_ 交互的接口
    void enqueue_raw_for_send(std::string&& raw_msg);
    bool is_outbound_queue_empty() const;


    // --- 事件处理函数 (委托给状态对象) ---
    void on_message_received(const FixMessage& msg);
    void on_timer_check(); // 从 private 移到 public
    void on_io_error(const std::string& reason);
    void on_shutdown(const std::string& reason);

    // 新方法：优雅地发起登出流程
    void initiate_logout(const std::string& reason);

    // 新增: 调度周期性的会话定时任务
    void schedule_timer_tasks(TimingWheel* wheel);

    // --- 状态转换方法 ---
    void changeState(std::unique_ptr<IStateHandler> newState);

    const std::string senderCompID;
    const std::string targetCompID;

    // 移至 public，以便 Worker 线程可以访问
    FixCodec codec_;

    // --- 公共辅助函数 (供状态类调用) ---
    void send_logout(const std::string& reason);
    void send_heartbeat(const std::string& test_req_id = "");
    void send_test_request(const std::string& id);
    void perform_shutdown(const std::string& reason);
    void update_last_recv_time();
    void update_last_send_time();
    std::chrono::steady_clock::time_point get_last_recv_time() const;
    std::chrono::steady_clock::time_point get_last_send_time() const;
    int get_heart_bt_int() const;
    void set_heart_bt_int(int new_hb);
    int get_min_heart_bt_int() const;
    int get_max_heart_bt_int() const;

    // --- 序列号管理 ---
    int get_send_seq_num() { return sendSeqNum; }
    int get_recv_seq_num() { return recvSeqNum; }
    void increment_send_seq_num() { sendSeqNum++; }
    void increment_recv_seq_num() { recvSeqNum++; }
    void set_recv_seq_num(int seq) { recvSeqNum = seq; }


private:
    std::atomic<bool> shutting_down_{false};
    std::recursive_mutex state_mutex_; // 用于保护状态转换

    std::unique_ptr<IStateHandler> currentState_; // 新的状态机指针

    void internal_send(const std::string& raw_msg);


    // FixCodec codec_; // 已移至 public

    int heartBtInt;
    const int minHeartBtInt_;
    const int maxHeartBtInt_;
    ShutdownCallback shutdown_callback_;
    std::weak_ptr<Connection> connection_; // 取代 SendCallback

    moodycamel::ConcurrentQueue<std::string> outbound_q_; // <--- 改为使用 moodycamel

    std::atomic<bool> running_{false};

    // --- 会话状态 (已被状态模式取代) ---
    // std::atomic<bool> established_{false};
    // std::atomic<bool> logout_initiated_{false};
    // std::string logout_reason_;
    // std::chrono::steady_clock::time_point logout_initiation_time_;

    int sendSeqNum = 1;
    int recvSeqNum = 1;
    // std::string awaitingTestReqId; // 状态将自己管理
    std::chrono::steady_clock::time_point lastRecv;
    std::chrono::steady_clock::time_point lastSend;

    // 定时任务相关
    TimingWheel* timing_wheel_ = nullptr;
    TimerTaskId timer_task_id_ = INVALID_TIMER_ID;
};
} // fix40 名称空间结束
