#include "fix/session.hpp"
#include "fix/fix_messages.hpp"
#include "core/connection.hpp"
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <string_view>
// #include <variant> // 1. 不再需要 variant

/* 2. 移除 std::visit 相关的辅助函数
// Helper for std::visit
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
*/

namespace fix40 {

Session::Session(const std::string& sender, const std::string& target, int hb, ShutdownCallback shutdown_cb)
    : senderCompID(sender),
      targetCompID(target),
      heartBtInt(hb),
      shutdown_callback_(std::move(shutdown_cb)) {}

Session::~Session() {
    if (is_running()) {
        stop();
    }
    // if (processor_thread_.joinable()) processor_thread_.join(); // 3. 移除 processor_thread
    // if (sender_thread_.joinable()) sender_thread_.join(); // 1. 移除 join
}

void Session::set_connection(std::weak_ptr<Connection> conn) {
    connection_ = std::move(conn);
}

void Session::start() {
    running_ = true;
    this->lastRecv = this->lastSend = std::chrono::steady_clock::now();

    // processor_thread_ = std::thread(&Session::run_processor, this); // 4. 移除 processor_thread 创建
    // sender_thread_ = std::thread(&Session::run_sender, this); // 2. 移除创建

    std::cout << "[Session] Started for " << senderCompID << " -> " << targetCompID << std::endl;
}

void Session::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    // event_q_.stop(); // 5. 移除 event_q 停止
    outbound_q_.stop();
}

void Session::send(FixMessage& msg) {
    if (shutting_down_.load()) { // Use load for atomic read
        std::cout << "[Session] Ignoring send on shutting down session." << std::endl;
        return;
    }
    // 1. 加锁，编码消息
    std::string raw_msg;
    {
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        msg.set(tags::MsgSeqNum, this->sendSeqNum++);
        this->lastSend = std::chrono::steady_clock::now();
        raw_msg = codec_.encode(msg);
    }

    std::cout << ">>> ATTEMPT SEND: " << raw_msg << std::endl;
    if (auto conn = connection_.lock()) {
        conn->send(raw_msg);
    }
}

void Session::enqueue_raw_for_send(std::string&& raw_msg) {
    outbound_q_.enqueue(std::move(raw_msg));
}

bool Session::is_outbound_queue_empty() const {
    return outbound_q_.empty();
}

void Session::handle_write_ready() {
    if (auto conn = connection_.lock()) {
        std::string to_send;
        while (outbound_q_.try_pop(to_send)) {
            conn->send(to_send);
        }
    }

    // This check is now robust. It's called when the write buffer might be empty.
    if (shutting_down_.load() && outbound_q_.empty()) {
        if (auto conn = connection_.lock()) {
            conn->shutdown(); // Gracefully close the connection
        }
    }
}


/* 6. 移除 post_event
void Session::post_event(Event event) {
    event_q_.enqueue(std::move(event));
}
*/

void Session::schedule_timer_tasks(TimingWheel* wheel) {
    // This pattern is used to create a self-rescheduling task.
    // The task is wrapped in a shared_ptr so the lambda can capture a
    // shared_ptr to itself, breaking the circular dependency at initialization.
    auto p_check_task = std::make_shared<std::function<void()>>();

    *p_check_task = [self = weak_from_this(), wheel, p_check_task] {
        if (auto session = self.lock()) {
            if (!session->is_running()) {
                return;
            }

            // --- Perform checks (same as before) ---
            auto now = std::chrono::steady_clock::now();
            auto since_last_recv = std::chrono::duration_cast<std::chrono::seconds>(now - session->lastRecv).count();
            auto since_last_send = std::chrono::duration_cast<std::chrono::seconds>(now - session->lastSend).count();

            // 7. 直接调用方法，而不是 post event
            if (since_last_send >= session->heartBtInt) {
                session->on_heartbeat_check();
            }
            if (since_last_recv >= static_cast<long>(session->heartBtInt * 1.5)) {
                session->on_liveness_check();
            }

            // --- Reschedule self for the next tick ---
            wheel->add_task(1000, *p_check_task);
        }
    };

    // Schedule the first execution of the task.
    wheel->add_task(1000, *p_check_task);
}

/* 8. 移除 run_processor 和 process_event
void Session::run_processor() {
    std::cout << "[Session] Processor started" << std::endl;
    try {
        while (running_) {
            Event event;
            if (event_q_.pop(event)) {
                process_event(event);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Processor thread caught exception: " << e.what() << std::endl;
    }
    std::cout << "[Session] Processor thread stopped" << std::endl;
}

void Session::process_event(const Event& event) {
    std::visit(overloaded {
        [this](const MessageReceivedEvent& e) { on_message_received(e.msg); },
        [this](const LivenessCheckEvent&) { on_liveness_check(); },
        [this](const HeartbeatCheckEvent&) { on_heartbeat_check(); },
        [this](const IoErrorEvent& e) { perform_shutdown("IO Error: " + e.reason); },
        [this](const ShutdownEvent& e) { perform_shutdown("Shutdown requested: " + e.reason); }
    }, event);
}
*/

/* 3. 移除 run_sender 的实现
void Session::run_sender() {
    try {
        while (running_) {
            FixMessage msg;
            if (outbound_q_.pop(msg)) {
                // 9. 在 sender 中也需要加锁来安全地访问和修改状态
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    msg.set(tags::MsgSeqNum, this->sendSeqNum++);
                    this->lastSend = std::chrono::steady_clock::now();
                }

                std::string raw_msg = codec_.encode(msg);
                std::cout << ">>> SEND: " << raw_msg << std::endl;
                send_callback_(raw_msg);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Sender thread caught exception: " << e.what() << std::endl;
    }
}
*/


void Session::on_message_received(const FixMessage& msg) {
    // 1. 检查是否正在关闭
    if (shutting_down_) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(state_mutex_); // 10. 加锁
    this->lastRecv = std::chrono::steady_clock::now();
    // Treat receiving a message as a sign of life that resets the heartbeat send timer as well.
    this->lastSend = this->lastRecv;

    // Restore sequence number validation
    const int seq = msg.get_int(tags::MsgSeqNum);
    if (seq < this->recvSeqNum) {
        perform_shutdown("Sequence number too low, expected " + std::to_string(this->recvSeqNum) + " but got " + std::to_string(seq));
        return;
    }
    this->recvSeqNum = seq + 1;

    const auto msg_type = msg.get_string(tags::MsgType);
    if (msg_type == "A") {
        std::cout << "Logon message received. Session active." << std::endl;
    } else if (msg_type == "0") {
        if (msg.has(tags::TestReqID) && msg.get_string(tags::TestReqID) == this->awaitingTestReqId) {
            this->awaitingTestReqId.clear();
        }
    } else if (msg_type == "1") {
        if (msg.has(tags::TestReqID)) {
            send_heartbeat(msg.get_string(tags::TestReqID));
        }
    } else if (msg_type == "5") {
        std::cout << "Logout received from peer." << std::endl;
        perform_shutdown("Logout received from peer");
    }
}

void Session::on_liveness_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_); // 10. 加锁
    if (!this->awaitingTestReqId.empty()) {
        perform_shutdown("Peer timeout (no response to TestRequest)");
    } else {
        send_test_request("LivenessCheck");
    }
}

void Session::on_heartbeat_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_); // 10. 加锁
    send_heartbeat();
}

// 11. 实现新的 on_io_error 和 on_shutdown 方法
void Session::on_io_error(const std::string& reason) {
    // 锁应该在 perform_shutdown 内部处理，这里不需要
    perform_shutdown("IO Error: " + reason);
}

void Session::on_shutdown(const std::string& reason) {
    // This is the final step, called when the connection is truly closed.
    // We now call the application-level callback.
    if (shutdown_callback_) {
        std::cout << "[Session] Executing final shutdown callback." << std::endl;
        shutdown_callback_();
    }
}


// 注意：现在所有 send_... 方法都不再直接调用 send()
// 它们只是准备好消息，然后调用公共的 send() 方法。
// 锁也由公共的 send() 方法统一管理。

void Session::perform_shutdown(const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (shutting_down_.exchange(true)) {
        return;
    }
    stop(); // Immediately signal that the session is no longer running logically.

    std::cout << "[Session] Shutting down: " << reason << std::endl;

    send_logout(reason);

    // If there was nothing in the queue after sending logout, we can try to shutdown immediately.
    // Otherwise, handle_write_ready() will handle the final shutdown.
    if (outbound_q_.empty()) {
        if (auto conn = connection_.lock()) {
            conn->shutdown();
        }
    }
}

// --- 内部发送实现 ---
void Session::send_logout(const std::string& reason) {
    auto logout_msg = create_logout_message(senderCompID, targetCompID, 0, reason); // Seq num will be set in send()
    send(logout_msg);
}

void Session::send_heartbeat(const std::string& test_req_id) {
    auto hb = create_heartbeat_message(senderCompID, targetCompID, 0, test_req_id); // Seq num will be set in send()
    send(hb);
}

void Session::send_test_request(const std::string& id) {
    auto tr = create_test_request_message(senderCompID, targetCompID, 0, id);
    {
        std::lock_guard<std::recursive_mutex> lock(state_mutex_);
        this->awaitingTestReqId = id;
    }
    send(tr);
}

} // namespace fix40