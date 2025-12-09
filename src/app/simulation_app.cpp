/**
 * @file simulation_app.cpp
 * @brief 模拟交易应用层实现
 */

#include "app/simulation_app.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"

namespace fix40 {

SimulationApp::SimulationApp() = default;

SimulationApp::~SimulationApp() {
    stop();
}

void SimulationApp::start() {
    engine_.start();
}

void SimulationApp::stop() {
    engine_.stop();
}

void SimulationApp::onLogon(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged on: " << sessionID.to_string();
    
    // 提交到撮合引擎队列
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGON, sessionID});
}

void SimulationApp::onLogout(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged out: " << sessionID.to_string();
    
    // 提交到撮合引擎队列
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sessionID});
}

void SimulationApp::fromApp(const FixMessage& msg, const SessionID& sessionID) {
    const std::string msg_type = msg.get_string(tags::MsgType);
    
    LOG() << "[SimulationApp] Received business message: MsgType=" << msg_type 
          << " from " << sessionID.to_string();
    
    // 根据消息类型创建事件并提交到队列
    // 这里只做轻量的入队操作，快速返回
    if (msg_type == "D") {
        engine_.submit(OrderEvent{OrderEventType::NEW_ORDER, sessionID, msg});
    } else if (msg_type == "F") {
        engine_.submit(OrderEvent{OrderEventType::CANCEL_REQUEST, sessionID, msg});
    } else if (msg_type == "G") {
        engine_.submit(OrderEvent{OrderEventType::REPLACE_REQUEST, sessionID, msg});
    } else {
        LOG() << "[SimulationApp] Unhandled message type: " << msg_type;
    }
}

void SimulationApp::toApp(FixMessage& msg, const SessionID& sessionID) {
    const std::string msg_type = msg.get_string(tags::MsgType);
    LOG() << "[SimulationApp] Sending business message: MsgType=" << msg_type
          << " via " << sessionID.to_string();
}

} // namespace fix40
