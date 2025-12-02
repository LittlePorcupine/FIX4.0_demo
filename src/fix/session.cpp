#include "fix/session.hpp"

#include <iostream>
#include <iomanip>
#include <utility>
#include <stdexcept>
#include <unordered_map>

#include "core/connection.hpp"
#include "fix/fix_messages.hpp"
#include "base/timing_wheel.hpp"
#include "base/config.hpp"


namespace fix40 {

// =================================================================================
// 状态类声明
// =================================================================================

class DisconnectedState;
class LogonSentState;
class EstablishedState;
class LogoutSentState;

// =================================================================================
// 断开状态
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
// 已发送Logon状态（客户端）
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
// 已建立状态
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
// 已发送Logout状态
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
// Session 类实现
// =================================================================================

Session::Session(const std::string& sender,
                 const std::string& target,
                 int hb,
                 ShutdownCallback shutdown_cb)
    : senderCompID(sender),
      targetCompID(target),
      heartBtInt(hb),
      minHeartBtInt_(Config::instance().get_int("fix_session", "min_heartbeat_interval", 5)),
      maxHeartBtInt_(Config::instance().get_int("fix_session", "max_heartbeat_interval", 120)),
      shutdown_callback_(std::move(shutdown_cb)) {

    // 初始状态为断开
    currentState_ = std::make_unique<DisconnectedState>();
    update_last_recv_time();
    update_last_send_time();
}

Session::~Session() {
    // 取消定时任务，避免访问已销毁的对象
    if (timing_wheel_ && timer_task_id_ != INVALID_TIMER_ID) {
        timing_wheel_->cancel_task(timer_task_id_);
    }
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
int Session::get_min_heart_bt_int() const { return minHeartBtInt_; }
int Session::get_max_heart_bt_int() const { return maxHeartBtInt_; }


// ... [其它辅助函数的实现]

void Session::schedule_timer_tasks(TimingWheel* wheel) {
    if (!wheel || !running_) return;

    timing_wheel_ = wheel;
    std::weak_ptr<Session> weak_self = shared_from_this();

    // 使用周期性任务，一次注册永久生效，直到被取消
    timer_task_id_ = wheel->add_periodic_task(1000, [weak_self]() {
        if (auto self = weak_self.lock()) {
            if (auto conn = self->connection_.lock()) {
                // 将定时任务派发到连接绑定的工作线程执行
                conn->dispatch([self]() {
                    if (self->is_running()) {
                        self->on_timer_check();
                    }
                });
            }
        }
        // 不再需要重新调度，周期性任务会自动重复执行
    });
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
// 状态类实现
// =================================================================================

// --- 断开状态 ---
void DisconnectedState::onMessageReceived(Session& context, const FixMessage& msg) {
    // 此逻辑主要用于服务器端。
    if (msg.get_string(tags::MsgType) == "A") {
        context.increment_recv_seq_num();

        if (context.senderCompID == "SERVER") {
            const int client_hb_interval = msg.get_int(tags::HeartBtInt);
            // 注意：真实实现应当从会话上下文获取最小和最大值，
            // 此处为了示例假设构造函数已保存这些值。
            // 假设 Session 已保存了这些值，
            // 但当前没有相应的 getter 可用。
            // 为简单起见暂时使用固定范围，更好的方式是提供 getter。
            const int server_min_hb = context.get_min_heart_bt_int();
            const int server_max_hb = context.get_max_heart_bt_int();

            if (client_hb_interval >= server_min_hb && client_hb_interval <= server_max_hb) {
                std::cout << "Client requested HeartBtInt=" << client_hb_interval 
                          << ". Accepted. Establishing session." << std::endl;
                
                context.set_heart_bt_int(client_hb_interval); // 采用客户端的心跳值
                
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
void DisconnectedState::onTimerCheck([[maybe_unused]] Session& context) {} // 无操作
void DisconnectedState::onSessionStart(Session& context) {
    // 客户端发起 Logon
    if (context.senderCompID == "CLIENT") {
        auto logon_msg = create_logon_message(context.senderCompID, context.targetCompID, 1, context.get_heart_bt_int());
        context.send(logon_msg);
        context.changeState(std::make_unique<LogonSentState>());
    } else {
        // 服务器端仅等待
        std::cout << "Server session started, waiting for client Logon." << std::endl;
    }
}
void DisconnectedState::onLogoutRequest([[maybe_unused]] Session& context, [[maybe_unused]] const std::string& reason) {} // 无操作

// --- 已发送 Logon 状态 ---
void LogonSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "A") {
        context.increment_recv_seq_num();
        std::cout << "Logon confirmation received. Session established." << std::endl;
        context.changeState(std::make_unique<EstablishedState>());
    } else {
        context.perform_shutdown("Received non-Logon message while waiting for Logon confirmation.");
    }
}
void LogonSentState::onTimerCheck([[maybe_unused]] Session& context) { /* 这里可以添加登录超时逻辑 */ }
void LogonSentState::onSessionStart([[maybe_unused]] Session& context) { /* 已处理 */ }
void LogonSentState::onLogoutRequest(Session& context, const std::string& reason) {
    context.perform_shutdown("Logout requested during logon process: " + reason);
}

// --- 已建立状态 ---
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
        // 处理业务消息或未知类型
        std::cout << "Received business message or unknown type: " << msg_type << std::endl;
    }
}

void EstablishedState::onTimerCheck(Session& context) {
    auto now = std::chrono::steady_clock::now();
    auto seconds_since_recv = std::chrono::duration_cast<std::chrono::seconds>(now - context.get_last_recv_time()).count();
    auto seconds_since_send = std::chrono::duration_cast<std::chrono::seconds>(now - context.get_last_send_time()).count();
    const int hb_interval = context.get_heart_bt_int();

    if (logout_initiated_ && 
        std::chrono::duration_cast<std::chrono::seconds>(now - logout_initiation_time_).count() >= 
            Config::instance().get_int("fix_session", "logout_confirm_timeout_sec", 10)) {
        context.perform_shutdown("Logout confirmation not received within timeout.");
        return;
    }

    // --- TestRequest 超时检查 ---
    if (!awaitingTestReqId_.empty() && seconds_since_recv >= 
        static_cast<long>(hb_interval * Config::instance().get_double("fix_session", "test_request_timeout_multiplier", 1.5))) {
        context.perform_shutdown("TestRequest timeout. No response from peer.");
        return;
    }

    // --- 独立检查 1：是否需要向对端发送心跳 ---
    // 如果在一个心跳间隔内没有发送任何数据，则立刻发送心跳
    if (seconds_since_send >= hb_interval) {
        context.send_heartbeat();
    }

    // --- 独立检查 2：是否需要测试对端连接 (TestRequest) ---
    // 如果长时间未收到任何消息，且当前没有等待 TestReq 响应，
    // 发送一个 TestRequest。
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

void EstablishedState::handleLogout(Session& context, [[maybe_unused]] const FixMessage& msg) {
    if (logout_initiated_) {
        // 我方发起了登出，这是确认。
        context.perform_shutdown("Logout confirmation received.");
    } else {
        // 对方发起了登出，我们确认后等待最终确认或超时。
        context.send_logout("Confirming peer's logout request");
        context.changeState(std::make_unique<LogoutSentState>("Peer initiated logout"));
    }
}

void EstablishedState::handleLogon(Session& context, [[maybe_unused]] const FixMessage& msg) {
    context.perform_shutdown("Logon not expected after session is established.");
}

// --- 已发送 Logout 状态 ---
LogoutSentState::LogoutSentState(std::string reason) 
    : reason_(std::move(reason)), initiation_time_(std::chrono::steady_clock::now()) {}

void LogoutSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "5") {
        context.increment_recv_seq_num();
        context.perform_shutdown("Logout confirmation received.");
    } else {
        std::cout << "Warning: Received non-Logout message while waiting for Logout confirmation." << std::endl;
        // 根据规范忽略其他消息
    }
}

void LogoutSentState::onTimerCheck(Session& context) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - initiation_time_).count() >= 
            Config::instance().get_int("fix_session", "logout_confirm_timeout_sec", 10)) {
        context.perform_shutdown("Logout confirmation not received within timeout. Reason: " + reason_);
    }
}

void LogoutSentState::onLogoutRequest([[maybe_unused]] Session& context, [[maybe_unused]] const std::string& reason) {
    // 已在登出过程中，不执行任何操作。
}

} // fix40 名称空间结束
