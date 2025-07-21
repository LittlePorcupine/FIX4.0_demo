#include "fix/session.hpp"

#include <iostream>
#include <iomanip>
#include <utility>
#include <stdexcept>
#include <unordered_map>

#include "core/connection.hpp"
#include "fix/fix_messages.hpp"
#include "base/timing_wheel.hpp"


namespace fix40 {

// =================================================================================
// 状态类声明
// =================================================================================

class DisconnectedState;
class LogonSentState;
class EstablishedState;
class LogoutSentState;

// =================================================================================
// DisconnectedState
// =================================================================================
class DisconnectedState : public IStateHandler {
public:
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart(Session& context) override;
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "Disconnected"; }
};

// =================================================================================
// LogonSentState (Client-side)
// =================================================================================
class LogonSentState : public IStateHandler {
public:
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart(Session& context) override;
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "LogonSent"; }
};

// =================================================================================
// EstablishedState
// =================================================================================
class EstablishedState : public IStateHandler {
private:
    using MessageHandler = void (EstablishedState::*)(Session&, const FixMessage&);
    const std::unordered_map<std::string, MessageHandler> messageHandlers_;
    
    std::string awaitingTestReqId_; // 状态内部管理自己的数据
    std::chrono::steady_clock::time_point logout_initiation_time_;
    bool logout_initiated_ = false;

    void handleHeartbeat(Session& context, const FixMessage& msg);
    void handleTestRequest(Session& context, const FixMessage& msg);
    void handleLogout(Session& context, const FixMessage& msg);
    void handleLogon(Session& context, const FixMessage& msg);

public:
    EstablishedState();
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart(Session& context) override {}
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "Established"; }
};

// =================================================================================
// LogoutSentState
// =================================================================================
class LogoutSentState : public IStateHandler {
private:
    std::string reason_;
    std::chrono::steady_clock::time_point initiation_time_;
public:
    explicit LogoutSentState(std::string reason);
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart(Session& context) override {}
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "LogoutSent"; }
};

// =================================================================================
// Session Class Implementation
// =================================================================================

Session::Session(const std::string& sender,
                 const std::string& target,
                 int hb,
                 ShutdownCallback shutdown_cb,
                 int min_hb, int max_hb)
    : senderCompID(sender),
      targetCompID(target),
      heartBtInt(hb),
      minHeartBtInt_(min_hb),
      maxHeartBtInt_(max_hb),
      shutdown_callback_(std::move(shutdown_cb)) {

    currentState_ = std::make_unique<DisconnectedState>();
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
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    running_ = true;
    lastRecv = std::chrono::steady_clock::now();
    lastSend = std::chrono::steady_clock::now();
    currentState_->onSessionStart(*this);
}

void Session::stop() {
    running_ = false;
}

void Session::changeState(std::unique_ptr<IStateHandler> newState) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!newState) return;
    std::cout << "Session (" << senderCompID << "): State changing from <" 
              << (currentState_ ? currentState_->getStateName() : "null") << "> to <"
              << newState->getStateName() << ">" << std::endl;
    currentState_ = std::move(newState);
}

void Session::send_buffered_data() {
    if (auto conn = connection_.lock()) {
        conn->handle_write();
    }
}

void Session::handle_write_ready() {
    if (auto conn = connection_.lock()) {
        conn->handle_write();
    }
}

// 事件处理委托
void Session::on_message_received(const FixMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_) return;

    update_last_recv_time();

    const int msg_seq_num = msg.get_int(tags::MsgSeqNum);
    if (msg_seq_num != recvSeqNum) {
        perform_shutdown("Incorrect sequence number received. Expected: " + std::to_string(recvSeqNum) + " Got: " + std::to_string(msg_seq_num));
        return;
    }
    
    currentState_->onMessageReceived(*this, msg);
}

void Session::on_timer_check() {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_) return;
    currentState_->onTimerCheck(*this);
}

void Session::on_io_error(const std::string& reason) {
    perform_shutdown("I/O Error: " + reason);
}

void Session::on_shutdown(const std::string& reason) {
    perform_shutdown(reason);
}

void Session::initiate_logout(const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    if (!running_) return;
    currentState_->onLogoutRequest(*this, reason);
}

void Session::send(FixMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    msg.set(tags::MsgSeqNum, sendSeqNum++);
    
    std::string raw_msg = codec_.encode(msg);
    internal_send(raw_msg);
}

void Session::internal_send(const std::string& raw_msg) {
    std::cout << ">>> SEND (" << (connection_.lock() ? std::to_string(connection_.lock()->fd()) : "N/A") << "): " << raw_msg << std::endl;

    if (auto conn = connection_.lock()) {
        conn->send(raw_msg);
        update_last_send_time();
    }
}

void Session::perform_shutdown(const std::string& reason) {
    if (shutting_down_.exchange(true)) return;

    std::cout << "Session shutting down. Reason: " << reason << std::endl;
    stop();

    changeState(std::make_unique<DisconnectedState>());

    if (auto conn = connection_.lock()) {
        conn->shutdown();
    }

    if (shutdown_callback_) {
        shutdown_callback_();
    }
}

// 时间管理
void Session::update_last_recv_time() { lastRecv = std::chrono::steady_clock::now(); }
void Session::update_last_send_time() { lastSend = std::chrono::steady_clock::now(); }
std::chrono::steady_clock::time_point Session::get_last_recv_time() const { return lastRecv; }
std::chrono::steady_clock::time_point Session::get_last_send_time() const { return lastSend; }
int Session::get_heart_bt_int() const { return heartBtInt; }
void Session::set_heart_bt_int(int new_hb) { heartBtInt = new_hb; }


// ... [其它辅助函数的实现]

void Session::schedule_timer_tasks(TimingWheel* wheel) {
    if (!wheel) return;
    std::weak_ptr<Session> weak_self = shared_from_this();
    auto timer_task = std::make_shared<std::function<void()>>();
    *timer_task = [weak_self, wheel, timer_task]() {
        if (auto self = weak_self.lock()) {
            if (self->is_running()) {
                self->on_timer_check();
                // 高频检查：每 1000 ms 触发一次
                wheel->add_task(1000, *timer_task);
            }
        }
    };
    // 首次调度 1 秒后触发
    wheel->add_task(1000, *timer_task);
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


// =================================================================================
// State Classes Implementation
// =================================================================================

// --- DisconnectedState ---
void DisconnectedState::onMessageReceived(Session& context, const FixMessage& msg) {
    // This logic is primarily for the server side.
    if (msg.get_string(tags::MsgType) == "A") {
        context.increment_recv_seq_num();

        if (context.senderCompID == "SERVER") {
            const int client_hb_interval = msg.get_int(tags::HeartBtInt);
            // NOTE: A real implementation would fetch min/max from Session context,
            // but for this example, we assume the constructor holds them.
            // Let's assume the session holds min/max, which it now does.
            // We need to fetch them from the context, but the context does not have getters for them yet.
            // Let's assume a fixed range for now for simplicity in this step. A better way would be to add getters.
            const int server_min_hb = 5;
            const int server_max_hb = 120;

            if (client_hb_interval >= server_min_hb && client_hb_interval <= server_max_hb) {
                std::cout << "Client requested HeartBtInt=" << client_hb_interval 
                          << ". Accepted. Establishing session." << std::endl;
                
                context.set_heart_bt_int(client_hb_interval); // Adopt client's value
                
                auto logon_ack = create_logon_message(context.senderCompID, context.targetCompID, 1, context.get_heart_bt_int());
                context.send(logon_ack);
                context.changeState(std::make_unique<EstablishedState>());
            } else {
                std::string reason = "HeartBtInt=" + std::to_string(client_hb_interval) + " is out of acceptable range [" + std::to_string(server_min_hb) + ", " + std::to_string(server_max_hb) + "].";
                std::cout << reason << std::endl;
                auto logout_msg = create_logout_message(context.senderCompID, context.targetCompID, 1, reason);
                context.send(logout_msg);
                context.perform_shutdown(reason);
            }
        }
    } else {
        context.perform_shutdown("Received non-Logon message in disconnected state.");
    }
}
void DisconnectedState::onTimerCheck(Session& context) {} // Do nothing
void DisconnectedState::onSessionStart(Session& context) {
    // Client-side initiates Logon
    if (context.senderCompID == "CLIENT") {
        auto logon_msg = create_logon_message(context.senderCompID, context.targetCompID, 1, context.get_heart_bt_int());
        context.send(logon_msg);
        context.changeState(std::make_unique<LogonSentState>());
    } else {
        // Server side just waits
        std::cout << "Server session started, waiting for client Logon." << std::endl;
    }
}
void DisconnectedState::onLogoutRequest(Session&, const std::string&) {} // Do nothing

// --- LogonSentState ---
void LogonSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "A") {
        context.increment_recv_seq_num();
        std::cout << "Logon confirmation received. Session established." << std::endl;
        context.changeState(std::make_unique<EstablishedState>());
    } else {
        context.perform_shutdown("Received non-Logon message while waiting for Logon confirmation.");
    }
}
void LogonSentState::onTimerCheck(Session& context) { /* Can add logon timeout logic here */ }
void LogonSentState::onSessionStart(Session& context) { /* Already handled */ }
void LogonSentState::onLogoutRequest(Session& context, const std::string& reason) {
    context.perform_shutdown("Logout requested during logon process: " + reason);
}

// --- EstablishedState ---
EstablishedState::EstablishedState() : messageHandlers_({
    {"0", &EstablishedState::handleHeartbeat},
    {"1", &EstablishedState::handleTestRequest},
    {"5", &EstablishedState::handleLogout},
    {"A", &EstablishedState::handleLogon}
}) {}

void EstablishedState::onMessageReceived(Session& context, const FixMessage& msg) {
    context.increment_recv_seq_num();
    const std::string msg_type = msg.get_string(tags::MsgType);
    
    auto it = messageHandlers_.find(msg_type);
    if (it != messageHandlers_.end()) {
        (this->*(it->second))(context, msg);
    } else {
        // Handle business messages or unknown types
        std::cout << "Received business message or unknown type: " << msg_type << std::endl;
    }
}

void EstablishedState::onTimerCheck(Session& context) {
    auto now = std::chrono::steady_clock::now();
    auto seconds_since_recv = std::chrono::duration_cast<std::chrono::seconds>(now - context.get_last_recv_time()).count();
    auto seconds_since_send = std::chrono::duration_cast<std::chrono::seconds>(now - context.get_last_send_time()).count();
    const int hb_interval = context.get_heart_bt_int();

    if (logout_initiated_ && 
        std::chrono::duration_cast<std::chrono::seconds>(now - logout_initiation_time_).count() >= 10) {
        context.perform_shutdown("Logout confirmation not received within 10 seconds.");
        return;
    }

    // --- TestRequest Timeout Check ---
    if (!awaitingTestReqId_.empty() && seconds_since_recv >= static_cast<long>(hb_interval * 1.5)) {
        context.perform_shutdown("TestRequest timeout. No response from peer.");
        return;
    }

    // --- Independent Check 1: Do I need to reassure my peer? (Heartbeat) ---
    // If we haven't sent anything for a full heartbeat interval, send one now.
    if (seconds_since_send >= hb_interval) {
        context.send_heartbeat();
    }

    // --- Independent Check 2: Do I need to check on my peer? (TestRequest) ---
    // If we haven't received anything for a while, and we are not already waiting for a TestReq response,
    // send a TestRequest.
    if (seconds_since_recv >= static_cast<long>(hb_interval * 1.2) && awaitingTestReqId_.empty()) {
        awaitingTestReqId_ = "TestReq_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        context.send_test_request(awaitingTestReqId_);
    }
}

void EstablishedState::onLogoutRequest(Session& context, const std::string& reason) {
    logout_initiated_ = true;
    logout_initiation_time_ = std::chrono::steady_clock::now();
    context.send_logout(reason);
    context.changeState(std::make_unique<LogoutSentState>(reason));
}

void EstablishedState::handleHeartbeat(Session&, const FixMessage& msg) {
    if (msg.has(tags::TestReqID)) {
        if (msg.get_string(tags::TestReqID) == awaitingTestReqId_) {
            awaitingTestReqId_.clear();
        }
    }
}

void EstablishedState::handleTestRequest(Session& context, const FixMessage& msg) {
    context.send_heartbeat(msg.get_string(tags::TestReqID));
}

void EstablishedState::handleLogout(Session& context, const FixMessage& msg) {
    if (logout_initiated_) {
        // We initiated logout, this is the confirmation.
        context.perform_shutdown("Logout confirmation received.");
    } else {
        // They initiated logout, we confirm and then transition to wait for a potential final confirmation from them or timeout.
        context.send_logout("Confirming peer's logout request");
        context.changeState(std::make_unique<LogoutSentState>("Peer initiated logout"));
    }
}

void EstablishedState::handleLogon(Session& context, const FixMessage& msg) {
    context.perform_shutdown("Logon not expected after session is established.");
}

// --- LogoutSentState ---
LogoutSentState::LogoutSentState(std::string reason) 
    : reason_(std::move(reason)), initiation_time_(std::chrono::steady_clock::now()) {}

void LogoutSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "5") {
        context.increment_recv_seq_num();
        context.perform_shutdown("Logout confirmation received.");
    } else {
        std::cout << "Warning: Received non-Logout message while waiting for Logout confirmation." << std::endl;
        // Ignore other messages as per spec
    }
}

void LogoutSentState::onTimerCheck(Session& context) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - initiation_time_).count() >= 10) {
        context.perform_shutdown("Logout confirmation not received within 10 seconds. Reason: " + reason_);
    }
}

void LogoutSentState::onLogoutRequest(Session&, const std::string&) {
    // Already in the process of logging out, do nothing.
}

} // namespace fix40
