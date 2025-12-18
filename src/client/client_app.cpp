/**
 * @file client_app.cpp
 * @brief 客户端 FIX Application 实现
 */

#include "client_app.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace fix40::client {

ClientApp::ClientApp(std::shared_ptr<ClientState> state, const std::string& userId)
    : state_(std::move(state))
    , userId_(userId) {
}

// ============================================================================
// Application 接口实现
// ============================================================================

void ClientApp::onLogon(const SessionID& sessionID) {
    LOG() << "[ClientApp] Logged on: " << sessionID.to_string();
    state_->setConnectionState(ConnectionState::LOGGED_IN);
    state_->setUserId(userId_);
    state_->addMessage("登录成功");
    
    // 登录后自动查询资金和持仓
    queryBalance();
    queryPositions();
    queryOrderHistory();
}

void ClientApp::onLogout(const SessionID& sessionID) {
    LOG() << "[ClientApp] Logged out: " << sessionID.to_string();
    state_->setConnectionState(ConnectionState::DISCONNECTED);
    state_->addMessage("已登出");
}

void ClientApp::fromApp(const FixMessage& msg, const SessionID& sessionID) {
    std::string msgType = msg.get_string(tags::MsgType);
    LOG() << "[ClientApp] Received MsgType=" << msgType << " from " << sessionID.to_string();
    
    if (msgType == "8") {
        handleExecutionReport(msg);
    } else if (msgType == "U2") {
        handleBalanceResponse(msg);
    } else if (msgType == "U4") {
        handlePositionResponse(msg);
    } else if (msgType == "U5") {
        handleAccountUpdate(msg);
    } else if (msgType == "U6") {
        handlePositionUpdate(msg);
    } else if (msgType == "U8") {
        handleInstrumentSearchResponse(msg);
    } else if (msgType == "U10") {
        handleOrderHistoryResponse(msg);
    } else if (msgType == "j") {
        // BusinessMessageReject
        std::string text = msg.has(tags::Text) ? msg.get_string(tags::Text) : "Unknown error";
        state_->setLastError(text);
        state_->addMessage("业务拒绝: " + text);
    } else {
        LOG() << "[ClientApp] Unknown message type: " << msgType;
    }
}

void ClientApp::toApp(FixMessage& msg, const SessionID& sessionID) {
    std::string msgType = msg.get_string(tags::MsgType);
    LOG() << "[ClientApp] Sending MsgType=" << msgType << " via " << sessionID.to_string();
}

void ClientApp::setSession(std::shared_ptr<Session> session) {
    session_ = session;
}

// ============================================================================
// 业务操作
// ============================================================================

std::string ClientApp::sendNewOrder(const std::string& symbol, const std::string& side,
                                     int64_t qty, double price, const std::string& ordType) {
    auto session = session_.lock();
    if (!session) {
        state_->setLastError("未连接");
        return "";
    }
    
    std::string clOrdID = generateClOrdID();
    
    FixMessage msg;
    msg.set(tags::MsgType, "D");
    msg.set(tags::ClOrdID, clOrdID);
    msg.set(tags::HandlInst, "1");  // Automated execution
    msg.set(tags::Symbol, symbol);
    msg.set(tags::Side, side);
    msg.set(tags::OrderQty, static_cast<int>(qty));
    msg.set(tags::OrdType, ordType);
    
    if (ordType == "2") {  // Limit order
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << price;
        msg.set(tags::Price, oss.str());
    }
    
    msg.set(tags::TimeInForce, "0");  // Day
    
    // TransactTime
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeOss;
    timeOss << std::put_time(std::gmtime(&time), "%Y%m%d-%H:%M:%S");
    msg.set(tags::TransactTime, timeOss.str());
    
    session->send_app_message(msg);
    
    // 添加到本地订单列表
    OrderInfo order;
    order.clOrdID = clOrdID;
    order.symbol = symbol;
    order.side = (side == "1") ? "BUY" : "SELL";
    order.price = price;
    order.orderQty = qty;
    order.state = OrderState::PENDING_NEW;
    state_->addOrder(order);
    
    state_->addMessage("下单: " + symbol + " " + order.side + " " + std::to_string(qty) + "@" + std::to_string(price));
    
    return clOrdID;
}

void ClientApp::sendCancelOrder(const std::string& origClOrdID,
                                 const std::string& symbol, const std::string& side) {
    auto session = session_.lock();
    if (!session) {
        state_->setLastError("未连接");
        return;
    }
    
    std::string clOrdID = generateClOrdID();
    
    FixMessage msg;
    msg.set(tags::MsgType, "F");
    msg.set(tags::ClOrdID, clOrdID);
    msg.set(tags::OrigClOrdID, origClOrdID);
    msg.set(tags::Symbol, symbol);
    msg.set(tags::Side, side);
    msg.set(tags::CxlType, "F");  // Full cancel
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeOss;
    timeOss << std::put_time(std::gmtime(&time), "%Y%m%d-%H:%M:%S");
    msg.set(tags::TransactTime, timeOss.str());
    
    session->send_app_message(msg);
    state_->addMessage("撤单: " + origClOrdID);
}

void ClientApp::queryBalance() {
    auto session = session_.lock();
    if (!session) return;
    
    FixMessage msg;
    msg.set(tags::MsgType, "U1");
    msg.set(tags::RequestID, std::to_string(requestIdCounter_++));
    
    session->send_app_message(msg);
}

void ClientApp::queryPositions() {
    auto session = session_.lock();
    if (!session) return;
    
    FixMessage msg;
    msg.set(tags::MsgType, "U3");
    msg.set(tags::RequestID, std::to_string(requestIdCounter_++));
    
    session->send_app_message(msg);
}

void ClientApp::queryOrderHistory() {
    auto session = session_.lock();
    if (!session) return;

    FixMessage msg;
    msg.set(tags::MsgType, "U9");
    msg.set(tags::RequestID, std::to_string(requestIdCounter_++));
    session->send_app_message(msg);
}

void ClientApp::searchInstruments(const std::string& pattern, int maxResults) {
    auto session = session_.lock();
    if (!session) return;
    
    FixMessage msg;
    msg.set(tags::MsgType, "U7");
    msg.set(tags::RequestID, std::to_string(requestIdCounter_++));
    msg.set(tags::SearchPattern, pattern);
    msg.set(tags::MaxResults, maxResults);
    
    session->send_app_message(msg);
}

// ============================================================================
// 消息处理
// ============================================================================

void ClientApp::handleExecutionReport(const FixMessage& msg) {
    OrderInfo order;
    order.clOrdID = msg.get_string(tags::ClOrdID);
    
    if (msg.has(tags::OrderID)) {
        order.orderId = msg.get_string(tags::OrderID);
    }
    if (msg.has(tags::Symbol)) {
        order.symbol = msg.get_string(tags::Symbol);
    }
    if (msg.has(tags::Side)) {
        order.side = (msg.get_string(tags::Side) == "1") ? "BUY" : "SELL";
    }
    if (msg.has(tags::Price)) {
        try {
            order.price = std::stod(msg.get_string(tags::Price));
        } catch (...) { order.price = 0.0; }
    }
    if (msg.has(tags::OrderQty)) {
        try {
            order.orderQty = std::stoll(msg.get_string(tags::OrderQty));
        } catch (...) { order.orderQty = 0; }
    }
    if (msg.has(tags::CumQty)) {
        try {
            order.filledQty = std::stoll(msg.get_string(tags::CumQty));
        } catch (...) { order.filledQty = 0; }
    }
    if (msg.has(tags::AvgPx)) {
        try {
            order.avgPx = std::stod(msg.get_string(tags::AvgPx));
        } catch (...) { order.avgPx = 0.0; }
    }
    if (msg.has(tags::Text)) {
        order.text = msg.get_string(tags::Text);
    }
    
    // 解析订单状态
    if (msg.has(tags::OrdStatus)) {
        std::string status = msg.get_string(tags::OrdStatus);
        if (status == "0") order.state = OrderState::NEW;
        else if (status == "1") order.state = OrderState::PARTIALLY_FILLED;
        else if (status == "2") order.state = OrderState::FILLED;
        else if (status == "4") order.state = OrderState::CANCELED;
        else if (status == "8") order.state = OrderState::REJECTED;
    }
    
    state_->updateOrder(order.clOrdID, order);
    
    // 生成消息
    std::string stateStr;
    switch (order.state) {
        case OrderState::NEW: stateStr = "已确认"; break;
        case OrderState::PARTIALLY_FILLED: stateStr = "部分成交"; break;
        case OrderState::FILLED: stateStr = "全部成交"; break;
        case OrderState::CANCELED: stateStr = "已撤销"; break;
        case OrderState::REJECTED: stateStr = "已拒绝"; break;
        default: stateStr = "未知"; break;
    }
    
    // 生成消息（拒绝时显示原因）
    if (order.state == OrderState::REJECTED && !order.text.empty()) {
        state_->addMessage("订单 " + order.clOrdID + " " + stateStr + ": " + order.text);
        state_->setLastError(order.text);
    } else {
        state_->addMessage("订单 " + order.clOrdID + " " + stateStr);
    }
    
    // 成交后刷新资金和持仓
    if (order.state == OrderState::FILLED || order.state == OrderState::PARTIALLY_FILLED) {
        queryBalance();
        queryPositions();
    }
}

void ClientApp::handleBalanceResponse(const FixMessage& msg) {
    AccountInfo info;
    
    auto safeStod = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };
    
    if (msg.has(tags::Balance)) {
        info.balance = safeStod(msg.get_string(tags::Balance));
    }
    if (msg.has(tags::Available)) {
        info.available = safeStod(msg.get_string(tags::Available));
    }
    if (msg.has(tags::FrozenMargin)) {
        info.frozenMargin = safeStod(msg.get_string(tags::FrozenMargin));
    }
    if (msg.has(tags::UsedMargin)) {
        info.usedMargin = safeStod(msg.get_string(tags::UsedMargin));
    }
    if (msg.has(tags::PositionProfit)) {
        info.positionProfit = safeStod(msg.get_string(tags::PositionProfit));
    }
    if (msg.has(tags::CloseProfit)) {
        info.closeProfit = safeStod(msg.get_string(tags::CloseProfit));
    }
    if (msg.has(tags::DynamicEquity)) {
        info.dynamicEquity = safeStod(msg.get_string(tags::DynamicEquity));
    }
    if (msg.has(tags::RiskRatio)) {
        info.riskRatio = safeStod(msg.get_string(tags::RiskRatio));
    }
    
    state_->updateAccount(info);
}

void ClientApp::handlePositionResponse(const FixMessage& msg) {
    // 简化处理：从 Text 字段解析持仓
    // 格式: "IF2601:L10@4000.00,S5@4100.00;IC2601:L20@5000.00,S0@0.00;"
    if (!msg.has(tags::Text)) {
        state_->clearPositions();
        return;
    }
    
    std::string text = msg.get_string(tags::Text);
    std::vector<PositionInfo> positions;
    
    std::istringstream iss(text);
    std::string item;
    while (std::getline(iss, item, ';')) {
        if (item.empty()) continue;
        
        auto colonPos = item.find(':');
        if (colonPos == std::string::npos) continue;
        
        PositionInfo pos;
        pos.instrumentId = item.substr(0, colonPos);
        
        std::string rest = item.substr(colonPos + 1);
        // 解析 L10@4000.00,S5@4100.00
        std::istringstream restIss(rest);
        std::string part;
        while (std::getline(restIss, part, ',')) {
            if (part.empty()) continue;
            char dir = part[0];
            auto atPos = part.find('@');
            if (atPos == std::string::npos) continue;
            
            try {
                int64_t qty = std::stoll(part.substr(1, atPos - 1));
                double price = std::stod(part.substr(atPos + 1));
                
                if (dir == 'L') {
                    pos.longPosition = qty;
                    pos.longAvgPrice = price;
                } else if (dir == 'S') {
                    pos.shortPosition = qty;
                    pos.shortAvgPrice = price;
                }
            } catch (...) {
                // 解析失败，跳过该字段
            }
        }
        
        if (pos.longPosition > 0 || pos.shortPosition > 0) {
            positions.push_back(pos);
        }
    }
    
    state_->setPositions(positions);
}

void ClientApp::handleAccountUpdate(const FixMessage& msg) {
    // 与 handleBalanceResponse 相同，但不触发额外的查询
    AccountInfo info;
    
    auto safeStod = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };
    
    if (msg.has(tags::Balance)) {
        info.balance = safeStod(msg.get_string(tags::Balance));
    }
    if (msg.has(tags::Available)) {
        info.available = safeStod(msg.get_string(tags::Available));
    }
    if (msg.has(tags::FrozenMargin)) {
        info.frozenMargin = safeStod(msg.get_string(tags::FrozenMargin));
    }
    if (msg.has(tags::UsedMargin)) {
        info.usedMargin = safeStod(msg.get_string(tags::UsedMargin));
    }
    if (msg.has(tags::PositionProfit)) {
        info.positionProfit = safeStod(msg.get_string(tags::PositionProfit));
    }
    if (msg.has(tags::CloseProfit)) {
        info.closeProfit = safeStod(msg.get_string(tags::CloseProfit));
    }
    if (msg.has(tags::DynamicEquity)) {
        info.dynamicEquity = safeStod(msg.get_string(tags::DynamicEquity));
    }
    if (msg.has(tags::RiskRatio)) {
        info.riskRatio = safeStod(msg.get_string(tags::RiskRatio));
    }
    
    state_->updateAccount(info);
}

void ClientApp::handlePositionUpdate(const FixMessage& msg) {
    PositionInfo pos;
    
    auto safeStod = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };
    
    if (msg.has(tags::InstrumentID)) {
        pos.instrumentId = msg.get_string(tags::InstrumentID);
    }
    if (msg.has(tags::LongPosition)) {
        pos.longPosition = msg.get_int(tags::LongPosition);
    }
    if (msg.has(tags::LongAvgPrice)) {
        pos.longAvgPrice = safeStod(msg.get_string(tags::LongAvgPrice));
    }
    if (msg.has(tags::ShortPosition)) {
        pos.shortPosition = msg.get_int(tags::ShortPosition);
    }
    if (msg.has(tags::ShortAvgPrice)) {
        pos.shortAvgPrice = safeStod(msg.get_string(tags::ShortAvgPrice));
    }
    if (msg.has(tags::PositionProfit)) {
        pos.profit = safeStod(msg.get_string(tags::PositionProfit));
    }
    
    state_->updatePosition(pos);
}

void ClientApp::handleInstrumentSearchResponse(const FixMessage& msg) {
    std::vector<std::string> results;
    
    if (msg.has(tags::InstrumentList)) {
        std::string list = msg.get_string(tags::InstrumentList);
        std::istringstream iss(list);
        std::string item;
        while (std::getline(iss, item, ',')) {
            if (!item.empty()) {
                results.push_back(item);
            }
        }
    }
    
    state_->setSearchResults(results);
}

void ClientApp::handleOrderHistoryResponse(const FixMessage& msg) {
    // Text 字段包含序列化订单列表（与 ClientState::saveOrders 的格式对齐）：
    // clOrdID|orderId|symbol|side|price|orderQty|filledQty|avgPx|state|text|updateTime
    if (!msg.has(tags::Text)) {
        state_->clearOrders();
        state_->addMessage("订单历史为空");
        return;
    }

    std::string text = msg.get_string(tags::Text);
    if (text.empty()) {
        state_->clearOrders();
        state_->addMessage("订单历史为空");
        return;
    }

    std::vector<OrderInfo> orders;
    std::istringstream iss(text);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;

        std::vector<std::string> fields;
        std::istringstream lineIss(line);
        std::string field;
        while (std::getline(lineIss, field, '|')) {
            fields.push_back(field);
        }

        if (fields.size() < 9) {
            continue;
        }

        OrderInfo order;
        order.clOrdID = fields[0];
        order.orderId = fields[1];
        order.symbol = fields[2];
        order.side = fields[3];

        auto safeStod = [](const std::string& s) -> double {
            try { return std::stod(s); } catch (...) { return 0.0; }
        };
        auto safeStoll = [](const std::string& s) -> int64_t {
            try { return std::stoll(s); } catch (...) { return 0; }
        };

        order.price = safeStod(fields[4]);
        order.orderQty = safeStoll(fields[5]);
        order.filledQty = safeStoll(fields[6]);
        order.avgPx = safeStod(fields[7]);

        int stateVal = 0;
        try {
            stateVal = std::stoi(fields[8]);
        } catch (...) {
            stateVal = 0;
        }
        if (stateVal < 0 || stateVal > static_cast<int>(OrderState::REJECTED)) {
            stateVal = 0;
        }
        order.state = static_cast<OrderState>(stateVal);

        if (fields.size() >= 10) {
            order.text = fields[9];
        }
        if (fields.size() >= 11) {
            order.updateTime = fields[10];
        }

        if (!order.clOrdID.empty()) {
            orders.push_back(std::move(order));
        }
    }

    state_->setOrders(orders);
    state_->addMessage("订单历史已刷新 (" + std::to_string(orders.size()) + ")");
}

std::string ClientApp::generateClOrdID() {
    std::ostringstream oss;
    oss << userId_ << "-" << std::setfill('0') << std::setw(6) << orderIdCounter_++;
    return oss.str();
}

} // namespace fix40::client
