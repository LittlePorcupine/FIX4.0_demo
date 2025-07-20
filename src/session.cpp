#include "fix/session.hpp"

#include <iostream>
#include <iomanip>
#include <utility>

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
        // 处理消息序号不匹配
        perform_shutdown("Incorrect sequence number received.");
        return;
    }
    recvSeqNum++;

    const std::string msg_type = msg.get_string(tags::MsgType);
    if (msg_type == "A") { // 登录
        // 登录验证成功
    } else if (msg_type == "0") { // 心跳
        if (msg.has(tags::TestReqID)) {
            if (msg.get_string(tags::TestReqID) == awaitingTestReqId) {
                awaitingTestReqId.clear();
            }
        }
    } else if (msg_type == "1") { // 测试请求
        send_heartbeat(msg.get_string(tags::TestReqID));
    } else if (msg_type == "5") { // 登出
        perform_shutdown("Logout message received from peer.");
    }
}

void Session::on_liveness_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_) return;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRecv).count() >= heartBtInt) {
        if (!awaitingTestReqId.empty()) {
            // 已经发送过测试请求但尚未收到回应
            // 这将在 on_heartbeat_check 中处理
            return;
        }
        awaitingTestReqId = "TestReq_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        send_test_request(awaitingTestReqId);
    }
}

void Session::on_heartbeat_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_) return;

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRecv).count() >= (heartBtInt * 2)) {
        perform_shutdown("No heartbeat from peer.");
    }

    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastSend).count() >= heartBtInt) {
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
    // 这是启动自身登出的入口
    // 先发送登出报文，再执行关闭逻辑，确保对端能收到
    send_logout(reason);
    perform_shutdown(reason);
}

void Session::schedule_timer_tasks(TimingWheel* wheel) {
    if (!wheel) return;
    wheel->add_task(heartBtInt * 1000, [self = shared_from_this()]() { self->on_liveness_check(); });
    wheel->add_task(heartBtInt * 1000, [self = shared_from_this()]() { self->on_heartbeat_check(); });
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
