#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory> // 以便使用 weak_ptr

#include "fix/fix_codec.hpp"
// #include "fix_events.hpp" // 2. 不再需要 events
#include "base/safe_queue.hpp"
#include "base/timing_wheel.hpp"

namespace fix40 {

class Connection;

class Session : public std::enable_shared_from_this<Session> {
public:
    using ShutdownCallback = std::function<void()>; // 不再需要传 fd

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


    // 移除 post_event
    // void post_event(Event event);

    // --- 3. 将事件处理函数声明为 public ---
    // --- 事件处理 ---
    void on_message_received(const FixMessage& msg);
    void on_liveness_check();
    void on_heartbeat_check();
    void on_io_error(const std::string& reason);
    void on_shutdown(const std::string& reason);

    // 新方法：优雅地发起登出流程
    void initiate_logout(const std::string& reason);

    // 新增: 调度周期性的会话定时任务
    void schedule_timer_tasks(TimingWheel* wheel);

    const std::string senderCompID;
    const std::string targetCompID;

    // 移至 public，以便 Worker 线程可以访问
    FixCodec codec_;

private:
    std::atomic<bool> shutting_down_{false}; // 1. 新增 shutting_down 标志

    // 1. 将 std::mutex 改为 std::recursive_mutex
    std::recursive_mutex state_mutex_;

    // --- 线程主函数 (将被移除) ---
    // void run_processor();
    // void run_sender(); // 1. 移除 run_sender 声明

    // --- 事件处理 (已移至 public) ---
    // void process_event(const Event& event);

    // --- 状态更新辅助函数 ---
    // void on_message_received(const FixMessage& msg); // 已移动
    // void on_liveness_check(); // 已移动
    // void on_heartbeat_check(); // 已移动
    void perform_shutdown(const std::string& reason);

    // --- 内部发送实现 ---
    void send_logout(const std::string& reason);
    void send_heartbeat(const std::string& test_req_id = "");
    void send_test_request(const std::string& id);

    // FixCodec codec_; // 已移至 public

    // 调整成员变量的声明顺序以匹配构造函数的初始化顺序，修复 -Wreorder 警告
    const int heartBtInt;
    ShutdownCallback shutdown_callback_;
    std::weak_ptr<Connection> connection_; // 取代 SendCallback

    // std::thread sender_thread_; // 2. 移除 sender_thread_

    // SafeQueue<Event> event_q_;
    SafeQueue<std::string> outbound_q_;

    std::atomic<bool> running_{false};

    // --- 会话状态 (现在由 state_mutex_ 保护) ---
    int sendSeqNum = 1;
    int recvSeqNum = 1;
    std::string awaitingTestReqId;
    std::chrono::steady_clock::time_point lastRecv;
    std::chrono::steady_clock::time_point lastSend;
};
} // fix40 名称空间结束
