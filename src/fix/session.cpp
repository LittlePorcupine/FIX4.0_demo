/**
 * @file session.cpp
 * @brief FIX 会话层实现
 *
 * 实现 Session 类和各状态处理器类。
 */

#include "fix/session.hpp"

#include <iomanip>
#include <utility>
#include <stdexcept>
#include <unordered_map>

#include "core/connection.hpp"
#include "fix/fix_messages.hpp"
#include "fix/application.hpp"
#include "base/timing_wheel.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include "storage/store.hpp"


namespace fix40 {

// =================================================================================
// 状态类声明
// =================================================================================

class DisconnectedState;
class LogonSentState;
class EstablishedState;
class LogoutSentState;

/**
 * @class DisconnectedState
 * @brief 断开连接状态
 *
 * 初始状态，等待会话启动或接收 Logon 消息。
 */
class DisconnectedState : public IStateHandler {
public:
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart(Session& context) override;
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "Disconnected"; }
};

/**
 * @class LogonSentState
 * @brief 已发送 Logon 状态（客户端）
 *
 * 客户端发送 Logon 后进入此状态，等待服务端确认。
 */
class LogonSentState : public IStateHandler {
public:
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart([[maybe_unused]] Session& context) override {}
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "LogonSent"; }
};

/**
 * @class EstablishedState
 * @brief 会话已建立状态
 *
 * 正常工作状态，处理心跳、TestRequest 和业务消息。
 */
class EstablishedState : public IStateHandler {
private:
    using MessageHandler = void (EstablishedState::*)(Session&, const FixMessage&);
    const std::unordered_map<std::string, MessageHandler> messageHandlers_; ///< 消息处理器映射
    
    std::string awaitingTestReqId_;  ///< 等待响应的 TestReqID
    std::chrono::steady_clock::time_point logout_initiation_time_; ///< 登出发起时间
    bool logout_initiated_ = false;  ///< 是否已发起登出

    /** @brief 处理 Heartbeat 消息 */
    void handleHeartbeat(Session& context, const FixMessage& msg);
    /** @brief 处理 TestRequest 消息 */
    void handleTestRequest(Session& context, const FixMessage& msg);
    /** @brief 处理 ResendRequest 消息 */
    void handleResendRequest(Session& context, const FixMessage& msg);
    /** @brief 处理 SequenceReset 消息 */
    void handleSequenceReset(Session& context, const FixMessage& msg);
    /** @brief 处理 Logout 消息 */
    void handleLogout(Session& context, const FixMessage& msg);
    /** @brief 处理 Logon 消息（异常情况） */
    void handleLogon(Session& context, const FixMessage& msg);

public:
    EstablishedState();
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart([[maybe_unused]] Session& context) override {}
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "Established"; }
};

/**
 * @class LogoutSentState
 * @brief 已发送 Logout 状态
 *
 * 发送 Logout 后进入此状态，等待对方确认或超时。
 */
class LogoutSentState : public IStateHandler {
private:
    std::string reason_;  ///< 登出原因
    std::chrono::steady_clock::time_point initiation_time_; ///< 发起时间
public:
    /**
     * @brief 构造 LogoutSentState
     * @param reason 登出原因
     */
    explicit LogoutSentState(std::string reason);
    void onMessageReceived(Session& context, const FixMessage& msg) override;
    void onTimerCheck(Session& context) override;
    void onSessionStart([[maybe_unused]] Session& context) override {}
    void onLogoutRequest(Session& context, const std::string& reason) override;
    const char* getStateName() const override { return "LogoutSent"; }
};

// =================================================================================
// Session 类实现
// =================================================================================

Session::Session(const std::string& sender,
                 const std::string& target,
                 int hb,
                 ShutdownCallback shutdown_cb,
                 IStore* store)
    : senderCompID(sender),
      targetCompID(target),
      heartBtInt(hb),
      minHeartBtInt_(Config::instance().get_int("fix_session", "min_heartbeat_interval", 5)),
      maxHeartBtInt_(Config::instance().get_int("fix_session", "max_heartbeat_interval", 120)),
      shutdown_callback_(std::move(shutdown_cb)),
      store_(store) {

    // 初始状态为断开
    currentState_ = std::make_unique<DisconnectedState>();
    update_last_recv_time();
    update_last_send_time();
    
    // 如果有存储接口，尝试恢复会话状态
    if (store_) {
        restore_session_state();
    }
}

Session::~Session() {
    // 取消定时任务，避免访问已销毁的对象
    if (timing_wheel_ && timer_task_id_ != INVALID_TIMER_ID) {
        timing_wheel_->cancel_task(timer_task_id_);
    }
    LOG() << "Session (" << senderCompID << " -> " << targetCompID << ") destroyed.";
}

void Session::set_connection(std::weak_ptr<Connection> conn) {
    connection_ = std::move(conn);
}

void Session::set_application(Application* app) {
    application_ = app;
}

Application* Session::get_application() const {
    return application_;
}

SessionID Session::get_session_id() const {
    return SessionID(senderCompID, targetCompID);
}

void Session::send_app_message(FixMessage& msg) {
    std::lock_guard<std::recursive_mutex> lock(state_mutex_);
    
    // 调用应用层的 toApp 回调
    if (application_) {
        try {
            application_->toApp(msg, get_session_id());
        } catch (const std::exception& e) {
            LOG() << "Application::toApp threw exception: " << e.what();
        } catch (...) {
            LOG() << "Application::toApp threw unknown exception";
        }
    }
    
    // 使用标准发送流程
    send(msg);
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
    LOG() << "Session (" << senderCompID << "): State changing from <" 
          << (currentState_ ? currentState_->getStateName() : "null") << "> to <"
          << newState->getStateName() << ">";
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
    const std::string msg_type = msg.get_string(tags::MsgType);
    
    // 检查是否是 SequenceReset 消息（特殊处理）
    if (msg_type == "4") {
        // SequenceReset 消息需要特殊处理，不检查序列号
        currentState_->onMessageReceived(*this, msg);
        return;
    }
    
    // 检查序列号
    if (msg_seq_num > recvSeqNum) {
        // 检测到序列号 gap，发送 ResendRequest
        LOG() << "Sequence number gap detected. Expected: " << recvSeqNum 
              << ", Got: " << msg_seq_num << ". Sending ResendRequest.";
        
        // 发送 ResendRequest 请求重传缺失的消息
        send_resend_request(recvSeqNum, msg_seq_num - 1);
        
        // 暂存当前消息，等待重传完成后处理
        // 注意：简化实现中，我们先处理当前消息，实际生产环境应该排队
        // 这里我们选择断开连接，让对方重新发送
        perform_shutdown("Sequence number gap detected. Expected: " + std::to_string(recvSeqNum) + 
                        " Got: " + std::to_string(msg_seq_num) + ". ResendRequest sent.");
        return;
    } else if (msg_seq_num < recvSeqNum) {
        // 收到的序列号小于期望值
        // 检查是否是重复消息（PossDupFlag = Y）
        if (msg.has(tags::PossDupFlag) && msg.get_string(tags::PossDupFlag) == "Y") {
            LOG() << "Ignoring duplicate message with SeqNum=" << msg_seq_num;
            return;
        }
        
        // 非重复消息但序列号过低，这是严重错误
        perform_shutdown("Received message with sequence number lower than expected. Expected: " + 
                        std::to_string(recvSeqNum) + " Got: " + std::to_string(msg_seq_num));
        return;
    }
    
    // 序列号正确，正常处理
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
    int seq_num = sendSeqNum++;
    msg.set(tags::MsgSeqNum, seq_num);
    
    std::string raw_msg = codec_.encode(msg);
    
    // 持久化消息（用于断线恢复时重传）
    if (store_) {
        StoredMessage stored_msg;
        stored_msg.seqNum = seq_num;
        stored_msg.senderCompID = senderCompID;
        stored_msg.targetCompID = targetCompID;
        stored_msg.msgType = msg.get_string(tags::MsgType);
        stored_msg.rawMessage = raw_msg;
        stored_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (!store_->saveMessage(stored_msg)) {
            LOG() << "Warning: Failed to persist message with SeqNum=" << seq_num;
        }
        
        // 保存会话状态
        save_session_state();
    }
    
    internal_send(raw_msg);
}

void Session::internal_send(const std::string& raw_msg) {
    LOG() << ">>> SEND (" << (connection_.lock() ? std::to_string(connection_.lock()->fd()) : "N/A") << "): " << raw_msg;

    if (auto conn = connection_.lock()) {
        conn->send(raw_msg);
        update_last_send_time();
    }
}

void Session::perform_shutdown(const std::string& reason) {
    if (shutting_down_.exchange(true)) return;

    LOG() << "Session shutting down. Reason: " << reason;
    
    // 通知应用层会话即将断开
    if (application_) {
        try {
            application_->onLogout(get_session_id());
        } catch (const std::exception& e) {
            LOG() << "Application::onLogout threw exception: " << e.what();
        } catch (...) {
            LOG() << "Application::onLogout threw unknown exception";
        }
    }
    
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
            // 从 Logon 消息中提取客户端标识
            // FIX 协议中，客户端发送的 Logon 消息的 SenderCompID 是客户端的标识
            std::string clientCompID = "UNKNOWN";
            if (msg.has(tags::SenderCompID)) {
                clientCompID = msg.get_string(tags::SenderCompID);
            }
            context.set_client_comp_id(clientCompID);
            LOG() << "Client CompID extracted from Logon: " << clientCompID;
            
            const int client_hb_interval = msg.get_int(tags::HeartBtInt);
            // 注意：真实实现应当从会话上下文获取最小和最大值，
            // 此处为了示例假设构造函数已保存这些值。
            // 假设 Session 已保存了这些值，
            // 但当前没有相应的 getter 可用。
            // 为简单起见暂时使用固定范围，更好的方式是提供 getter。
            const int server_min_hb = context.get_min_heart_bt_int();
            const int server_max_hb = context.get_max_heart_bt_int();

            if (client_hb_interval >= server_min_hb && client_hb_interval <= server_max_hb) {
                LOG() << "Client requested HeartBtInt=" << client_hb_interval 
                      << ". Accepted. Establishing session.";
                
                context.set_heart_bt_int(client_hb_interval); // 采用客户端的心跳值
                
                auto logon_ack = create_logon_message(context.senderCompID, context.targetCompID, 1, context.get_heart_bt_int());
                context.send(logon_ack);
                context.changeState(std::make_unique<EstablishedState>());
                
                // 通知应用层会话已建立
                if (Application* app = context.get_application()) {
                    try {
                        app->onLogon(context.get_session_id());
                    } catch (const std::exception& e) {
                        LOG() << "Application::onLogon threw exception: " << e.what();
                    } catch (...) {
                        LOG() << "Application::onLogon threw unknown exception";
                    }
                }
            } else {
                std::string reason = "HeartBtInt=" + std::to_string(client_hb_interval) + " is out of acceptable range [" + std::to_string(server_min_hb) + ", " + std::to_string(server_max_hb) + "].";
                LOG() << reason;
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
        LOG() << "Server session started, waiting for client Logon.";
    }
}
void DisconnectedState::onLogoutRequest([[maybe_unused]] Session& context, [[maybe_unused]] const std::string& reason) {} // 无操作

// --- 已发送 Logon 状态 ---
void LogonSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "A") {
        context.increment_recv_seq_num();
        LOG() << "Logon confirmation received. Session established.";
        context.changeState(std::make_unique<EstablishedState>());
        
        // 通知应用层会话已建立
        if (Application* app = context.get_application()) {
            try {
                app->onLogon(context.get_session_id());
            } catch (const std::exception& e) {
                LOG() << "Application::onLogon threw exception: " << e.what();
            } catch (...) {
                LOG() << "Application::onLogon threw unknown exception";
            }
        }
    } else {
        context.perform_shutdown("Received non-Logon message while waiting for Logon confirmation.");
    }
}
void LogonSentState::onTimerCheck([[maybe_unused]] Session& context) { /* 这里可以添加登录超时逻辑 */ }
void LogonSentState::onLogoutRequest(Session& context, const std::string& reason) {
    context.perform_shutdown("Logout requested during logon process: " + reason);
}

// --- 已建立状态 ---
EstablishedState::EstablishedState() : messageHandlers_({
    {"0", &EstablishedState::handleHeartbeat},
    {"1", &EstablishedState::handleTestRequest},
    {"2", &EstablishedState::handleResendRequest},
    {"4", &EstablishedState::handleSequenceReset},
    {"5", &EstablishedState::handleLogout},
    {"A", &EstablishedState::handleLogon}
}) {}

void EstablishedState::onMessageReceived(Session& context, const FixMessage& msg) {
    const std::string msg_type = msg.get_string(tags::MsgType);
    
    // SequenceReset 消息不递增 recvSeqNum，由 handleSequenceReset 处理序列号更新
    if (msg_type != "4") {
        context.increment_recv_seq_num();
    }
    
    auto it = messageHandlers_.find(msg_type);
    if (it != messageHandlers_.end()) {
        // 会话层消息，由状态机处理
        (this->*(it->second))(context, msg);
        
        // 通知应用层收到管理消息（可选回调）
        if (Application* app = context.get_application()) {
            try {
                app->fromAdmin(msg, context.get_session_id());
            } catch (const std::exception& e) {
                LOG() << "Application::fromAdmin threw exception: " << e.what();
            } catch (...) {
                LOG() << "Application::fromAdmin threw unknown exception";
            }
        }
    } else {
        // 业务消息，委托给应用层处理
        Application* app = context.get_application();
        if (app) {
            try {
                app->fromApp(msg, context.get_session_id());
            } catch (const std::exception& e) {
                LOG() << "Application::fromApp threw exception: " << e.what();
                // 可选：发送 BusinessMessageReject
            } catch (...) {
                LOG() << "Application::fromApp threw unknown exception";
            }
        } else {
            LOG() << "Received business message (MsgType=" << msg_type 
                  << ") but no Application is set. Message ignored.";
        }
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

void EstablishedState::handleResendRequest(Session& context, const FixMessage& msg) {
    int begin_seq_no = msg.get_int(tags::BeginSeqNo);
    int end_seq_no = msg.get_int(tags::EndSeqNo);
    
    LOG() << "Received ResendRequest: BeginSeqNo=" << begin_seq_no << ", EndSeqNo=" << end_seq_no;
    
    IStore* store = context.get_store();
    if (!store) {
        // 没有存储，无法重传，发送 SequenceReset-GapFill 跳过所有请求的消息
        LOG() << "No store available for message resend. Sending SequenceReset-GapFill.";
        context.send_sequence_reset_gap_fill(begin_seq_no, context.get_send_seq_num());
        return;
    }
    
    // 如果 end_seq_no 为 0，表示请求到最新
    if (end_seq_no == 0) {
        end_seq_no = context.get_send_seq_num() - 1;
    }
    
    // 从存储加载消息
    auto messages = store->loadMessages(context.senderCompID, context.targetCompID, 
                                        begin_seq_no, end_seq_no);
    
    if (messages.empty()) {
        // 没有找到消息，发送 SequenceReset-GapFill
        LOG() << "No messages found for resend. Sending SequenceReset-GapFill.";
        context.send_sequence_reset_gap_fill(begin_seq_no, end_seq_no + 1);
        return;
    }
    
    // 重传消息
    context.set_processing_resend(true);
    
    int gap_fill_start = -1;
    int last_seq = begin_seq_no - 1;
    
    for (const auto& stored_msg : messages) {
        // 检查是否有序列号 gap（消息丢失）
        if (stored_msg.seqNum > last_seq + 1) {
            // 发送 GapFill 跳过缺失的消息
            if (gap_fill_start == -1) {
                gap_fill_start = last_seq + 1;
            }
        }
        
        // 检查是否是管理消息
        if (is_admin_message(stored_msg.msgType)) {
            // 管理消息用 GapFill 跳过
            if (gap_fill_start == -1) {
                gap_fill_start = stored_msg.seqNum;
            }
        } else {
            // 业务消息需要重传
            // 先发送之前累积的 GapFill
            if (gap_fill_start != -1) {
                context.send_sequence_reset_gap_fill(gap_fill_start, stored_msg.seqNum);
                gap_fill_start = -1;
            }
            
            // 重传业务消息（添加 PossDupFlag）
            LOG() << "Resending message SeqNum=" << stored_msg.seqNum;
            
            // 解码原始消息，添加 PossDupFlag，重新编码发送
            FixMessage resend_msg = context.codec_.decode(stored_msg.rawMessage);
            resend_msg.set(tags::PossDupFlag, "Y");
            resend_msg.set(tags::OrigSendingTime, resend_msg.get_string(tags::SendingTime));
            
            std::string raw_msg = context.codec_.encode(resend_msg);
            // 直接发送，不更新序列号
            if (auto conn = context.get_connection().lock()) {
                conn->send(raw_msg);
            }
        }
        
        last_seq = stored_msg.seqNum;
    }
    
    // 发送最后的 GapFill（如果有）
    if (gap_fill_start != -1) {
        context.send_sequence_reset_gap_fill(gap_fill_start, end_seq_no + 1);
    }
    
    // 检查是否还有未覆盖的序列号
    if (last_seq < end_seq_no) {
        context.send_sequence_reset_gap_fill(last_seq + 1, end_seq_no + 1);
    }
    
    context.set_processing_resend(false);
}

void EstablishedState::handleSequenceReset(Session& context, const FixMessage& msg) {
    int new_seq_no = msg.get_int(tags::NewSeqNo);
    int msg_seq_num = msg.get_int(tags::MsgSeqNum);
    bool is_gap_fill = msg.has(tags::GapFillFlag) && msg.get_string(tags::GapFillFlag) == "Y";
    
    LOG() << "Received SequenceReset: MsgSeqNum=" << msg_seq_num 
          << ", NewSeqNo=" << new_seq_no 
          << ", GapFill=" << (is_gap_fill ? "Y" : "N");
    
    // 基本验证：NewSeqNo 必须大于 0
    if (new_seq_no <= 0) {
        LOG() << "Error: Invalid NewSeqNo=" << new_seq_no << ". Must be positive.";
        context.perform_shutdown("Invalid SequenceReset: NewSeqNo must be positive");
        return;
    }
    
    if (is_gap_fill) {
        // GapFill 模式：只能向前移动序列号
        int current_recv_seq = context.get_recv_seq_num();
        if (new_seq_no > current_recv_seq) {
            context.set_recv_seq_num(new_seq_no);
            LOG() << "Updated expected receive sequence number to " << new_seq_no;
        } else if (new_seq_no < current_recv_seq) {
            LOG() << "Warning: SequenceReset-GapFill with NewSeqNo=" << new_seq_no 
                  << " is less than expected " << current_recv_seq << ". Ignoring.";
        }
    } else {
        // Reset 模式：可以重置到任意值（但仍需大于 0，已在上面验证）
        context.set_recv_seq_num(new_seq_no);
        LOG() << "Reset expected receive sequence number to " << new_seq_no;
    }
}

// --- 已发送 Logout 状态 ---
LogoutSentState::LogoutSentState(std::string reason) 
    : reason_(std::move(reason)), initiation_time_(std::chrono::steady_clock::now()) {}

void LogoutSentState::onMessageReceived(Session& context, const FixMessage& msg) {
    if (msg.get_string(tags::MsgType) == "5") {
        context.increment_recv_seq_num();
        context.perform_shutdown("Logout confirmation received.");
    } else {
        LOG() << "Warning: Received non-Logout message while waiting for Logout confirmation.";
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

// =================================================================================
// 断线恢复相关方法实现
// =================================================================================

void Session::send_resend_request(int begin_seq_no, int end_seq_no) {
    auto rr_msg = create_resend_request_message(senderCompID, targetCompID, 0, begin_seq_no, end_seq_no);
    send(rr_msg);
    LOG() << "Sent ResendRequest: BeginSeqNo=" << begin_seq_no << ", EndSeqNo=" << end_seq_no;
}

void Session::send_sequence_reset_gap_fill(int seq_num, int new_seq_no) {
    auto sr_msg = create_sequence_reset_message(senderCompID, targetCompID, seq_num, new_seq_no, true);
    
    // 注意：根据 FIX 协议，SequenceReset-GapFill 消息不应设置 PossDupFlag="Y"
    // GapFill 是一种特殊消息，用于跳过序列号，而不是重传
    
    // 直接编码发送，不递增序列号（因为这是重传流程的一部分）
    std::string raw_msg = codec_.encode(sr_msg);
    internal_send(raw_msg);
    
    LOG() << "Sent SequenceReset-GapFill: SeqNum=" << seq_num << ", NewSeqNo=" << new_seq_no;
}

void Session::save_session_state() {
    if (!store_) return;
    
    SessionState state;
    state.senderCompID = senderCompID;
    state.targetCompID = targetCompID;
    state.sendSeqNum = sendSeqNum;
    state.recvSeqNum = recvSeqNum;
    state.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    if (!store_->saveSessionState(state)) {
        LOG() << "Warning: Failed to save session state";
    }
}

bool Session::restore_session_state() {
    if (!store_) return false;
    
    auto state = store_->loadSessionState(senderCompID, targetCompID);
    if (!state) {
        LOG() << "No saved session state found for " << senderCompID << " -> " << targetCompID;
        return false;
    }
    
    sendSeqNum = state->sendSeqNum;
    recvSeqNum = state->recvSeqNum;
    
    LOG() << "Restored session state: SendSeqNum=" << sendSeqNum 
          << ", RecvSeqNum=" << recvSeqNum;
    
    return true;
}

} // fix40 名称空间结束
