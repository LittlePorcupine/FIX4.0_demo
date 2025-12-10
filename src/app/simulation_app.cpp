/**
 * @file simulation_app.cpp
 * @brief 模拟交易应用层实现
 */

#include "app/simulation_app.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"
#include <cstdlib>

namespace fix40 {

// ============================================================================
// FIX 消息解析辅助函数
// ============================================================================

namespace {

/**
 * @brief 从 FIX 消息解析 Order 结构
 */
Order parseNewOrderSingle(const FixMessage& msg, const SessionID& sessionID) {
    Order order;
    order.sessionID = sessionID;
    
    // 必填字段
    order.clOrdID = msg.get_string(tags::ClOrdID);
    order.symbol = msg.get_string(tags::Symbol);
    
    // Side: 1=Buy, 2=Sell
    std::string sideStr = msg.get_string(tags::Side);
    order.side = (sideStr == "1") ? OrderSide::BUY : OrderSide::SELL;
    
    // OrderQty
    if (msg.has(tags::OrderQty)) {
        order.orderQty = std::stoll(msg.get_string(tags::OrderQty));
    }
    
    // OrdType: 1=Market, 2=Limit
    std::string ordTypeStr = msg.get_string(tags::OrdType);
    order.ordType = (ordTypeStr == "1") ? OrderType::MARKET : OrderType::LIMIT;
    
    // Price (限价单必填)
    if (msg.has(tags::Price)) {
        order.price = std::stod(msg.get_string(tags::Price));
    }
    
    // TimeInForce: 0=Day, 1=GTC, 3=IOC, 4=FOK
    if (msg.has(tags::TimeInForce)) {
        std::string tifStr = msg.get_string(tags::TimeInForce);
        if (tifStr == "0") order.timeInForce = TimeInForce::DAY;
        else if (tifStr == "1") order.timeInForce = TimeInForce::GTC;
        else if (tifStr == "3") order.timeInForce = TimeInForce::IOC;
        else if (tifStr == "4") order.timeInForce = TimeInForce::FOK;
    }
    
    // 初始化执行状态
    order.status = OrderStatus::PENDING_NEW;
    order.cumQty = 0;
    order.leavesQty = order.orderQty;
    order.avgPx = 0.0;
    order.createTime = std::chrono::system_clock::now();
    order.updateTime = order.createTime;
    
    return order;
}

/**
 * @brief 从 FIX 消息解析 CancelRequest 结构
 */
CancelRequest parseCancelRequest(const FixMessage& msg, const SessionID& sessionID) {
    CancelRequest req;
    req.sessionID = sessionID;
    
    req.clOrdID = msg.get_string(tags::ClOrdID);
    req.origClOrdID = msg.get_string(tags::OrigClOrdID);
    
    if (msg.has(tags::Symbol)) {
        req.symbol = msg.get_string(tags::Symbol);
    }
    
    return req;
}

} // anonymous namespace

// ============================================================================
// SimulationApp 实现
// ============================================================================

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
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGON, sessionID});
}

void SimulationApp::onLogout(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged out: " << sessionID.to_string();
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sessionID});
}

void SimulationApp::fromApp(const FixMessage& msg, const SessionID& sessionID) {
    const std::string msgType = msg.get_string(tags::MsgType);
    
    LOG() << "[SimulationApp] Received business message: MsgType=" << msgType 
          << " from " << sessionID.to_string();
    
    // 解析 FIX 消息，转换为内部结构，提交到队列
    if (msgType == "D") {
        // NewOrderSingle
        Order order = parseNewOrderSingle(msg, sessionID);
        engine_.submit(OrderEvent::newOrder(order));
    } else if (msgType == "F") {
        // OrderCancelRequest
        CancelRequest req = parseCancelRequest(msg, sessionID);
        engine_.submit(OrderEvent::cancelRequest(req));
    } else {
        LOG() << "[SimulationApp] Unhandled message type: " << msgType;
    }
}

void SimulationApp::toApp(FixMessage& msg, const SessionID& sessionID) {
    const std::string msgType = msg.get_string(tags::MsgType);
    LOG() << "[SimulationApp] Sending business message: MsgType=" << msgType
          << " via " << sessionID.to_string();
}

} // namespace fix40
