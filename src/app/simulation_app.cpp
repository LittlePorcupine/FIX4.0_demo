/**
 * @file simulation_app.cpp
 * @brief 模拟交易应用层实现
 */

#include "app/simulation_app.hpp"
#include "app/fix_message_builder.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"
#include <cstdlib>

namespace fix40 {

// ============================================================================
// FIX 消息解析辅助函数
// ============================================================================

namespace {

/**
 * @brief 解析结果
 */
struct ParseResult {
    bool success = false;
    std::string error;
    Order order;
};

/**
 * @brief 从 FIX 消息解析 Order 结构
 * @return ParseResult 包含解析结果和错误信息
 */
ParseResult parseNewOrderSingle(const FixMessage& msg, const SessionID& sessionID) {
    ParseResult result;
    Order& order = result.order;
    order.sessionID = sessionID;
    
    // 必填字段：ClOrdID
    if (!msg.has(tags::ClOrdID)) {
        result.error = "Missing required field: ClOrdID(11)";
        return result;
    }
    order.clOrdID = msg.get_string(tags::ClOrdID);
    
    // 必填字段：Symbol
    if (!msg.has(tags::Symbol)) {
        result.error = "Missing required field: Symbol(55)";
        return result;
    }
    order.symbol = msg.get_string(tags::Symbol);
    
    // 必填字段：Side (1=Buy, 2=Sell)
    if (!msg.has(tags::Side)) {
        result.error = "Missing required field: Side(54)";
        return result;
    }
    std::string sideStr = msg.get_string(tags::Side);
    if (sideStr == "1") {
        order.side = OrderSide::BUY;
    } else if (sideStr == "2") {
        order.side = OrderSide::SELL;
    } else {
        result.error = "Invalid Side(54) value: " + sideStr + " (expected 1 or 2)";
        return result;
    }
    
    // 必填字段：OrderQty
    if (!msg.has(tags::OrderQty)) {
        result.error = "Missing required field: OrderQty(38)";
        return result;
    }
    try {
        order.orderQty = std::stoll(msg.get_string(tags::OrderQty));
        if (order.orderQty <= 0) {
            result.error = "Invalid OrderQty(38): must be positive";
            return result;
        }
    } catch (const std::exception& e) {
        result.error = "Invalid OrderQty(38) format: " + std::string(e.what());
        return result;
    }
    
    // 必填字段：OrdType (1=Market, 2=Limit)
    if (!msg.has(tags::OrdType)) {
        result.error = "Missing required field: OrdType(40)";
        return result;
    }
    std::string ordTypeStr = msg.get_string(tags::OrdType);
    if (ordTypeStr == "1") {
        order.ordType = OrderType::MARKET;
    } else if (ordTypeStr == "2") {
        order.ordType = OrderType::LIMIT;
    } else {
        LOG() << "[SimulationApp] Warning: Unknown OrdType(40)=" << ordTypeStr << ", defaulting to LIMIT";
        order.ordType = OrderType::LIMIT;
    }
    
    // Price (限价单必填)
    if (msg.has(tags::Price)) {
        try {
            order.price = std::stod(msg.get_string(tags::Price));
        } catch (const std::exception& e) {
            result.error = "Invalid Price(44) format: " + std::string(e.what());
            return result;
        }
    } else if (order.ordType == OrderType::LIMIT) {
        result.error = "Missing required field: Price(44) for limit order";
        return result;
    }
    
    // TimeInForce: 0=Day, 1=GTC, 3=IOC, 4=FOK (可选，默认 DAY)
    if (msg.has(tags::TimeInForce)) {
        std::string tifStr = msg.get_string(tags::TimeInForce);
        if (tifStr == "0") order.timeInForce = TimeInForce::DAY;
        else if (tifStr == "1") order.timeInForce = TimeInForce::GTC;
        else if (tifStr == "3") order.timeInForce = TimeInForce::IOC;
        else if (tifStr == "4") order.timeInForce = TimeInForce::FOK;
        else {
            LOG() << "[SimulationApp] Warning: Unknown TimeInForce(59)=" << tifStr << ", defaulting to DAY";
            order.timeInForce = TimeInForce::DAY;
        }
    }
    
    // 初始化执行状态
    order.status = OrderStatus::PENDING_NEW;
    order.cumQty = 0;
    order.leavesQty = order.orderQty;
    order.avgPx = 0.0;
    order.createTime = std::chrono::system_clock::now();
    order.updateTime = order.createTime;
    
    result.success = true;
    return result;
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

SimulationApp::SimulationApp() {
    // 设置 ExecutionReport 回调
    engine_.setExecutionReportCallback(
        [this](const SessionID& sid, const ExecutionReport& rpt) {
            onExecutionReport(sid, rpt);
        });
}

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
        ParseResult result = parseNewOrderSingle(msg, sessionID);
        if (result.success) {
            engine_.submit(OrderEvent::newOrder(result.order));
        } else {
            LOG() << "[SimulationApp] Rejected NewOrderSingle: " << result.error;
            // TODO: 发送 ExecutionReport 拒绝订单
        }
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

void SimulationApp::onExecutionReport(const SessionID& sessionID, const ExecutionReport& report) {
    LOG() << "[SimulationApp] Sending ExecutionReport to " << sessionID.to_string()
          << " ClOrdID=" << report.clOrdID
          << " OrdStatus=" << static_cast<int>(report.ordStatus);
    
    // 将 ExecutionReport 转换为 FIX 消息
    FixMessage msg = buildExecutionReport(report);
    
    // 通过 SessionManager 发送
    if (!sessionManager_.sendMessage(sessionID, msg)) {
        LOG() << "[SimulationApp] Failed to send ExecutionReport: session not found "
              << sessionID.to_string();
    }
}

} // namespace fix40
