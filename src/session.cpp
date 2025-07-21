#include "fix/session.hpp"

#include <iostream>
#include <iomanip>
#include <utility>
#include <stdexcept>

#include "core/connection.hpp"
#include "fix/fix_messages.hpp"
#include "base/timing_wheel.hpp"


namespace fix40 {

Session::Session(const std::string& sender,
                 const std::string& target,
                 int hb,
                 ShutdownCallback shutdown_cb)
    : senderCompID(sender),
      targetCompID(target),
      heartBtInt(hb),
      shutdown_callback_(std::move(shutdown_cb)) {

    lastRecv = std::chrono::steady_clock::now();
    lastSend = std::chrono::steady_clock::now();
}

Session::~Session() {
    std::cout << "Session (" << senderCompID << " -> " << targetCompID << ") destroyed." << std::endl;
}

void Session::set_connection(std::weak_ptr<Connection> conn) {
    connection_ = std::move(conn);
}

void Session::start() {
    running_ = true;
    lastRecv = std::chrono::steady_clock::now();
    lastSend = std::chrono::steady_clock::now();
    std::cout << "Session started." << std::endl;

    // 如果是客户端（Initiator），则主动发送 Logon
    if (senderCompID == "CLIENT") {
        auto logon_msg = create_logon_message(senderCompID, targetCompID, 1, heartBtInt);
        send(logon_msg);
        std::cout << "Logon message sent." << std::endl;
    }
}

void Session::stop() {
    running_ = false;
}

void Session::send(FixMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    msg.set(tags::MsgSeqNum, sendSeqNum++);
    
    std::string raw_msg = codec_.encode(msg);
    std::cout << ">>> SEND (" << (connection_.lock() ? std::to_string(connection_.lock()->fd()) : "N/A") << "): " << raw_msg << std::endl;

    if (auto conn = connection_.lock()) {
        conn->send(raw_msg);
        lastSend = std::chrono::steady_clock::now();
    }
}

void Session::send_buffered_data() {
    if (auto conn = connection_.lock()) {
        conn->handle_write();
    }
}

void Session::on_message_received(const FixMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    lastRecv = std::chrono::steady_clock::now();

    const int msg_seq_num = msg.get_int(tags::MsgSeqNum);
    if (msg_seq_num != recvSeqNum) {
        perform_shutdown("Incorrect sequence number received.");
        return;
    }
    recvSeqNum++;

    const std::string msg_type = msg.get_string(tags::MsgType);

    if (!established_.load()) {
        // --- 会话未建立时的逻辑 ---
        if (msg_type != "A") {
            perform_shutdown("First message must be Logon.");
            return;
        }

        // 收到 Logon 消息
        if (senderCompID == "SERVER") { // 服务器端逻辑
            // 验证通过，回复 Logon 并建立会话
            auto logon_ack = create_logon_message(senderCompID, targetCompID, 1, heartBtInt);
            send(logon_ack);
            established_ = true;
        } else { // 客户端逻辑
            // 收到服务器的 Logon 确认，建立会话
            established_ = true;
        }
    } else {
        // --- 会话已建立时的逻辑 ---
        if (msg_type == "A") {
            perform_shutdown("Logon not expected after session is established.");
            return;
        }

        if (msg_type == "0") { // 心跳
            if (msg.has(tags::TestReqID)) {
                if (msg.get_string(tags::TestReqID) == awaitingTestReqId) {
                    awaitingTestReqId.clear();
                }
            }
        } else if (msg_type == "1") { // 测试请求
            send_heartbeat(msg.get_string(tags::TestReqID));
        } else if (msg_type == "5") { // 登出
            if (logout_initiated_.load()) {
                perform_shutdown("Logout confirmation received.");
            } else {
                initiate_logout("Logout confirmation");
            }
        }
    }
}

void Session::on_timer_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_ || !established_.load()) return; // 在会话建立前不检查

    auto now = std::chrono::steady_clock::now();
    auto seconds_since_recv = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecv).count();
    auto seconds_since_send = std::chrono::duration_cast<std::chrono::seconds>(now - lastSend).count();

    // 0. 检查登出超时
    if (logout_initiated_.load() && 
        std::chrono::duration_cast<std::chrono::seconds>(now - logout_initiation_time_).count() >= 10) {
        perform_shutdown("Logout confirmation not received within 10 seconds. " + logout_reason_);
        return;
    }

    // 检查是否已发送 TestRequest 并超时
    if (!awaitingTestReqId.empty() && seconds_since_recv >= static_cast<long>(heartBtInt * 1.5)) {
        perform_shutdown("TestRequest timeout. No response from peer.");
        return; // 执行了关闭操作，直接返回
    }

    // 检查是否需要发送 TestRequest (例如，1.2 倍心跳间隔未收到任何消息)
    if (seconds_since_recv >= static_cast<long>(heartBtInt * 1.2) && awaitingTestReqId.empty()) {
        awaitingTestReqId = "TestReq_" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        send_test_request(awaitingTestReqId);
    }
    // 检查是否需要发送 Heartbeat (距离上次发送超过一个心跳间隔)
    else if (seconds_since_send >= heartBtInt) {
        send_heartbeat();
    }
}


void Session::on_io_error(const std::string& reason) {
    perform_shutdown("I/O Error: " + reason);
}

void Session::on_shutdown(const std::string& reason) {
    perform_shutdown(reason);
}

void Session::initiate_logout(const std::string& reason) {
    if (logout_initiated_.exchange(true)) return;

    logout_reason_ = reason;
    logout_initiation_time_ = std::chrono::steady_clock::now();
    send_logout(reason);
}

void Session::schedule_timer_tasks(TimingWheel* wheel) {
    if (!wheel) return;

    std::weak_ptr<Session> weak_self = shared_from_this();

    auto timer_task = std::make_shared<std::function<void()>>();

    *timer_task = [weak_self, wheel, timer_task]() {
        if (auto self = weak_self.lock()) {
            if (self->is_running()) {
                self->on_timer_check();
                wheel->add_task(self->heartBtInt * 1000, *timer_task);
            }
        }
    };

    wheel->add_task(heartBtInt * 1000, *timer_task);
}

void Session::perform_shutdown(const std::string& reason) {
    if (shutting_down_.exchange(true)) return;

    std::cout << "Session shutting down. Reason: " << reason << std::endl;
    stop();

    if (auto conn = connection_.lock()) {
        conn->shutdown();
    }

    if (shutdown_callback_) {
        shutdown_callback_();
    }
}

void Session::send_logout(const std::string& reason) {
    auto logout_msg = create_logout_message(senderCompID, targetCompID, 0, reason);
    send(logout_msg);
}

void Session::send_heartbeat(const std::string& test_req_id) {
    auto hb_msg = create_heartbeat_message(senderCompID, targetCompID, 0, test_req_id);
    send(hb_msg);
}

void Session::send_test_request(const std::string& id) {
    auto tr_msg = create_test_request_message(senderCompID, targetCompID, 0, id);
    send(tr_msg);
}


} // namespace fix40
