/**
 * @file matching_engine.cpp
 * @brief 撮合引擎实现
 *
 * 实现行情驱动撮合模式：用户订单与CTP行情盘口比对撮合。
 */

#include "app/engine/matching_engine.hpp"
#include "app/manager/risk_manager.hpp"
#include "app/manager/instrument_manager.hpp"
#include "base/logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fix40 {

namespace {

const char* sideToString(OrderSide side) {
    return side == OrderSide::BUY ? "Buy" : "Sell";
}

const char* ordTypeToString(OrderType type) {
    return type == OrderType::MARKET ? "Market" : "Limit";
}

const char* tifToString(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::DAY: return "Day";
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
        default: return "Unknown";
    }
}

} // anonymous namespace

// =============================================================================
// 辅助函数：构建拒绝单 ExecutionReport
// =============================================================================

ExecutionReport buildRejectReport(const Order& order, RejectReason reason, const std::string& text) {
    ExecutionReport report;
    report.orderID = order.orderID;
    report.clOrdID = order.clOrdID;
    report.execID = "";  // 调用方需要设置
    report.symbol = order.symbol;
    report.side = order.side;
    report.orderQty = order.orderQty;
    report.price = order.price;
    report.ordType = order.ordType;
    report.ordStatus = OrderStatus::REJECTED;
    report.ordRejReason = static_cast<int>(reason);
    report.text = text;
    report.transactTime = std::chrono::system_clock::now();
    report.execTransType = ExecTransType::NEW;
    return report;
}

MatchingEngine::MatchingEngine() = default;

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    if (running_.exchange(true)) {
        return;  // 已经在运行
    }
    
    worker_thread_ = std::thread([this]() { run(); });
    LOG() << "[MatchingEngine] Started";
}

void MatchingEngine::stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    // 提交一个空事件唤醒阻塞的 wait_dequeue
    event_queue_.enqueue(OrderEvent{});
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    LOG() << "[MatchingEngine] Stopped";
}

void MatchingEngine::submit(const OrderEvent& event) {
    event_queue_.enqueue(event);
}

void MatchingEngine::submit(OrderEvent&& event) {
    event_queue_.enqueue(std::move(event));
}

void MatchingEngine::run() {
    while (running_.load()) {
        // 先处理行情数据
        MarketData md;
        while (marketDataQueue_.try_dequeue(md)) {
            if (!running_.load()) break;
            try {
                handleMarketData(md);
            } catch (const std::exception& e) {
                LOG() << "[MatchingEngine] Exception processing market data: " << e.what();
            } catch (...) {
                LOG() << "[MatchingEngine] Unknown exception processing market data";
            }
        }

        // 处理订单事件
        OrderEvent event;
        if (event_queue_.wait_dequeue_timed(event, std::chrono::milliseconds(10))) {
            if (!running_.load()) break;
            
            try {
                process_event(event);
            } catch (const std::exception& e) {
                LOG() << "[MatchingEngine] Exception processing event: " << e.what();
            } catch (...) {
                LOG() << "[MatchingEngine] Unknown exception processing event";
            }
        }
    }
}

void MatchingEngine::process_event(const OrderEvent& event) {
    switch (event.type) {
        case OrderEventType::NEW_ORDER:
            handle_new_order(event);
            break;
        case OrderEventType::CANCEL_REQUEST:
            handle_cancel_request(event);
            break;
        case OrderEventType::SESSION_LOGON:
            handle_session_logon(event);
            break;
        case OrderEventType::SESSION_LOGOUT:
            handle_session_logout(event);
            break;
        default:
            LOG() << "[MatchingEngine] Unknown event type";
            break;
    }
}

void MatchingEngine::handle_new_order(const OrderEvent& event) {
    const Order* orderPtr = event.getOrder();
    if (!orderPtr) {
        LOG() << "[MatchingEngine] Invalid NEW_ORDER event: no order data";
        return;
    }
    
    // 复制订单
    Order order = *orderPtr;
    order.orderID = generateOrderID();
    order.leavesQty = order.orderQty;
    order.status = OrderStatus::PENDING_NEW;
    
    LOG() << "[MatchingEngine] Processing NewOrderSingle from " << event.sessionID.to_string();
    LOG() << "  ClOrdID: " << order.clOrdID;
    LOG() << "  Symbol: " << order.symbol;
    LOG() << "  Side: " << sideToString(order.side);
    LOG() << "  OrderQty: " << order.orderQty;
    LOG() << "  Price: " << order.price;
    LOG() << "  OrdType: " << ordTypeToString(order.ordType);
    LOG() << "  TimeInForce: " << tifToString(order.timeInForce);
    
    // userId 由上层 Application 在收到业务消息时完成身份绑定与校验。
    // 撮合引擎仍做一次防御性检查，避免产生无法路由或无法归属的订单状态。
    if (event.userId.empty()) {
        LOG() << "[MatchingEngine] Order rejected: empty userId";
        order.status = OrderStatus::REJECTED;
        auto report = buildRejectReport(order, RejectReason::NONE, "Invalid user identity");
        report.execID = generateExecID();
        sendExecutionReport(event.sessionID, report);
        return;
    }

    // 记录订单与会话的映射
    orderSessionMap_[order.clOrdID] = event.sessionID;
    // 记录订单与用户ID的映射（用于日志/追踪；资金与持仓更新由上层处理）
    orderUserMap_[order.clOrdID] = event.userId;
    
    // 获取行情快照
    auto snapshotIt = marketSnapshots_.find(order.symbol);
    MarketDataSnapshot snapshot;
    if (snapshotIt != marketSnapshots_.end()) {
        snapshot = snapshotIt->second;
    } else {
        snapshot.instrumentId = order.symbol;
    }
    
    // 尝试立即撮合
    bool matched = false;
    double fillPrice = 0.0;
    
    if (order.ordType == OrderType::MARKET) {
        // 市价单处理
        if (order.side == OrderSide::BUY) {
            if (snapshot.hasAsk()) {
                matched = true;
                fillPrice = snapshot.askPrice1;
            } else {
                // 无对手盘，拒绝市价单
                LOG() << "[MatchingEngine] Market order rejected: no ask side";
                order.status = OrderStatus::REJECTED;
                
                auto report = buildRejectReport(order, RejectReason::NO_COUNTER_PARTY, "No counter party (ask side empty)");
                report.execID = generateExecID();
                
                sendExecutionReport(event.sessionID, report);
                orderSessionMap_.erase(order.clOrdID); orderUserMap_.erase(order.clOrdID);
                return;
            }
        } else {
            if (snapshot.hasBid()) {
                matched = true;
                fillPrice = snapshot.bidPrice1;
            } else {
                // 无对手盘，拒绝市价单
                LOG() << "[MatchingEngine] Market order rejected: no bid side";
                order.status = OrderStatus::REJECTED;
                
                auto report = buildRejectReport(order, RejectReason::NO_COUNTER_PARTY, "No counter party (bid side empty)");
                report.execID = generateExecID();
                
                sendExecutionReport(event.sessionID, report);
                orderSessionMap_.erase(order.clOrdID); orderUserMap_.erase(order.clOrdID);
                return;
            }
        }
    } else {
        // 限价单处理
        if (order.side == OrderSide::BUY) {
            if (canMatchBuyOrder(order, snapshot)) {
                matched = true;
                fillPrice = snapshot.askPrice1;
            }
        } else {
            if (canMatchSellOrder(order, snapshot)) {
                matched = true;
                fillPrice = snapshot.bidPrice1;
            }
        }
    }
    
    if (matched) {
        // 立即成交
        order.status = OrderStatus::NEW;  // 先设为NEW，executeFill会更新为FILLED
        executeFill(order, fillPrice, order.orderQty);
        orderSessionMap_.erase(order.clOrdID); orderUserMap_.erase(order.clOrdID);
    } else {
        // 挂单等待
        order.status = OrderStatus::NEW;
        order.updateTime = std::chrono::system_clock::now();
        
        // 发送订单确认
        ExecutionReport report;
        report.orderID = order.orderID;
        report.clOrdID = order.clOrdID;
        report.execID = generateExecID();
        report.symbol = order.symbol;
        report.side = order.side;
        report.orderQty = order.orderQty;
        report.price = order.price;
        report.ordType = order.ordType;
        report.ordStatus = OrderStatus::NEW;
        report.cumQty = 0;
        report.avgPx = 0.0;
        report.leavesQty = order.leavesQty;
        report.transactTime = order.updateTime;
        report.execTransType = ExecTransType::NEW;
        
        LOG() << "[MatchingEngine] Order " << order.clOrdID << " acknowledged, pending for market data";
        sendExecutionReport(event.sessionID, report);
        
        // 添加到挂单列表
        addToPendingOrders(order);
    }
}

void MatchingEngine::handle_cancel_request(const OrderEvent& event) {
    const CancelRequest* req = event.getCancelRequest();
    if (!req) {
        LOG() << "[MatchingEngine] Invalid CANCEL_REQUEST event: no request data";
        return;
    }
    
    LOG() << "[MatchingEngine] Processing OrderCancelRequest from " << event.sessionID.to_string();
    LOG() << "  ClOrdID: " << req->clOrdID;
    LOG() << "  OrigClOrdID: " << req->origClOrdID;
    LOG() << "  Symbol: " << req->symbol;
    
    ExecutionReport report;
    report.clOrdID = req->clOrdID;
    report.origClOrdID = req->origClOrdID;
    report.execID = generateExecID();
    report.symbol = req->symbol;
    report.transactTime = std::chrono::system_clock::now();
    
    // 首先尝试从挂单列表中撤单（行情驱动模式）
    auto canceledOrder = removeFromPendingOrders(req->symbol, req->origClOrdID);
    
    if (!canceledOrder) {
        // 如果挂单列表中没有，尝试从传统订单簿中撤单（兼容模式）
        auto bookIt = orderBooks_.find(req->symbol);
        if (bookIt != orderBooks_.end()) {
            canceledOrder = bookIt->second->cancelOrder(req->origClOrdID);
        }
    }
    
    if (canceledOrder) {
        // 撤单成功
        report.orderID = canceledOrder->orderID;
        report.side = canceledOrder->side;
        report.orderQty = canceledOrder->orderQty;
        report.price = canceledOrder->price;
        report.ordType = canceledOrder->ordType;
        report.ordStatus = OrderStatus::CANCELED;
        report.cumQty = canceledOrder->cumQty;
        report.avgPx = canceledOrder->avgPx;
        report.leavesQty = 0;
        report.execTransType = ExecTransType::NEW;  // FIX 4.0: NEW + CANCELED status
        
        LOG() << "[MatchingEngine] Order " << req->origClOrdID << " canceled";
        
        // 清理映射
        orderSessionMap_.erase(req->origClOrdID); orderUserMap_.erase(req->origClOrdID);
    } else {
        // 撤单失败（订单不存在或已成交）
        report.ordStatus = OrderStatus::REJECTED;
        report.execTransType = ExecTransType::NEW;
        report.text = "Order not found or already filled";
        
        LOG() << "[MatchingEngine] Cancel rejected: order " << req->origClOrdID << " not found";
    }
    
    sendExecutionReport(event.sessionID, report);
}

void MatchingEngine::handle_session_logon(const OrderEvent& event) {
    LOG() << "[MatchingEngine] Session logged on: " << event.sessionID.to_string();
    // 会话登录时可以初始化交易状态
}

void MatchingEngine::handle_session_logout(const OrderEvent& event) {
    LOG() << "[MatchingEngine] Session logged out: " << event.sessionID.to_string();
    
    // 清理该会话的订单映射（可选：也可以保留用于重连恢复）
    // 这里简单处理，不主动撤单
}

OrderBook& MatchingEngine::getOrCreateOrderBook(const std::string& symbol) {
    auto it = orderBooks_.find(symbol);
    if (it == orderBooks_.end()) {
        auto [newIt, inserted] = orderBooks_.emplace(
            symbol, std::make_unique<OrderBook>(symbol));
        LOG() << "[MatchingEngine] Created OrderBook for " << symbol;
        return *newIt->second;
    }
    return *it->second;
}

const OrderBook* MatchingEngine::getOrderBook(const std::string& symbol) const {
    auto it = orderBooks_.find(symbol);
    if (it != orderBooks_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void MatchingEngine::sendExecutionReport(const SessionID& sessionID, const ExecutionReport& report) {
    if (execReportCallback_) {
        try {
            execReportCallback_(sessionID, report);
        } catch (const std::exception& e) {
            LOG() << "[MatchingEngine] Exception in ExecutionReport callback: " << e.what();
        } catch (...) {
            LOG() << "[MatchingEngine] Unknown exception in ExecutionReport callback";
        }
    } else {
        LOG() << "[MatchingEngine] No ExecutionReport callback set, report dropped";
    }
}

std::string MatchingEngine::generateExecID() {
    std::ostringstream oss;
    oss << "EXEC-" << std::setfill('0') << std::setw(10) << nextExecID_++;
    return oss.str();
}

std::string MatchingEngine::generateOrderID() {
    std::ostringstream oss;
    oss << "ORD-" << std::setfill('0') << std::setw(10) << nextOrderID_++;
    return oss.str();
}

// =============================================================================
// 行情驱动撮合实现
// =============================================================================

void MatchingEngine::submitMarketData(const MarketData& md) {
    marketDataQueue_.enqueue(md);
}

const MarketDataSnapshot* MatchingEngine::getMarketSnapshot(const std::string& instrumentId) const {
    auto it = marketSnapshots_.find(instrumentId);
    if (it != marketSnapshots_.end()) {
        return &it->second;
    }
    return nullptr;
}

const std::list<Order>* MatchingEngine::getPendingOrders(const std::string& instrumentId) const {
    auto it = pendingOrders_.find(instrumentId);
    if (it != pendingOrders_.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t MatchingEngine::getTotalPendingOrderCount() const {
    size_t count = 0;
    for (const auto& [instrumentId, orders] : pendingOrders_) {
        count += orders.size();
    }
    return count;
}

void MatchingEngine::handleMarketData(const MarketData& md) {
    std::string instrumentId = md.getInstrumentID();
    
    // 1. 更新行情快照
    MarketDataSnapshot& snapshot = marketSnapshots_[instrumentId];
    snapshot.instrumentId = instrumentId;
    snapshot.lastPrice = md.lastPrice;
    snapshot.bidPrice1 = md.bidPrice1;
    snapshot.bidVolume1 = md.bidVolume1;
    snapshot.askPrice1 = md.askPrice1;
    snapshot.askVolume1 = md.askVolume1;
    snapshot.upperLimitPrice = md.upperLimitPrice;
    snapshot.lowerLimitPrice = md.lowerLimitPrice;
    snapshot.updateTime = std::chrono::system_clock::now();

    // 2. 更新合约管理器中的涨跌停价格
    if (instrumentManager_) {
        instrumentManager_->updateLimitPrices(instrumentId, md.upperLimitPrice, md.lowerLimitPrice);
    }

    // 3. 触发行情更新回调（用于账户价值重算）
    if (marketDataUpdateCallback_ && md.lastPrice > 0) {
        marketDataUpdateCallback_(instrumentId, md.lastPrice);
    }

    // 4. 遍历该合约的挂单，检查是否可成交
    auto it = pendingOrders_.find(instrumentId);
    if (it == pendingOrders_.end() || it->second.empty()) {
        return;
    }

    auto& orders = it->second;
    auto orderIt = orders.begin();
    
    while (orderIt != orders.end()) {
        Order& order = *orderIt;
        
        // 尝试撮合
        if (tryMatch(order, snapshot)) {
            // 成交后移除订单
            orderSessionMap_.erase(order.clOrdID); orderUserMap_.erase(order.clOrdID);
            orderIt = orders.erase(orderIt);
        } else {
            ++orderIt;
        }
    }
}

bool MatchingEngine::tryMatch(Order& order, const MarketDataSnapshot& snapshot) {
    bool canMatch = false;
    double fillPrice = 0.0;
    
    if (order.side == OrderSide::BUY) {
        canMatch = canMatchBuyOrder(order, snapshot);
        if (canMatch) {
            fillPrice = snapshot.askPrice1;  // 买单以卖一价成交
        }
    } else {
        canMatch = canMatchSellOrder(order, snapshot);
        if (canMatch) {
            fillPrice = snapshot.bidPrice1;  // 卖单以买一价成交
        }
    }
    
    if (canMatch) {
        executeFill(order, fillPrice, order.leavesQty);
        return true;
    }
    
    return false;
}

bool MatchingEngine::canMatchBuyOrder(const Order& order, const MarketDataSnapshot& snapshot) const {
    // 买单成交条件：买价 >= CTP卖一价，且卖盘非空
    if (!snapshot.hasAsk()) {
        return false;
    }
    
    if (order.ordType == OrderType::MARKET) {
        // 市价单只要有卖盘就可成交
        return true;
    }
    
    // 限价单：买价 >= 卖一价
    return order.price >= snapshot.askPrice1;
}

bool MatchingEngine::canMatchSellOrder(const Order& order, const MarketDataSnapshot& snapshot) const {
    // 卖单成交条件：卖价 <= CTP买一价，且买盘非空
    if (!snapshot.hasBid()) {
        return false;
    }
    
    if (order.ordType == OrderType::MARKET) {
        // 市价单只要有买盘就可成交
        return true;
    }
    
    // 限价单：卖价 <= 买一价
    return order.price <= snapshot.bidPrice1;
}

void MatchingEngine::executeFill(Order& order, double fillPrice, int64_t fillQty) {
    // 计算加权平均成交价（在更新cumQty之前计算）
    int64_t prevCumQty = order.cumQty;
    double prevAvgPx = order.avgPx;
    
    // 更新订单状态
    order.cumQty += fillQty;
    order.leavesQty = order.orderQty - order.cumQty;
    
    // 计算加权平均成交价
    if (order.cumQty > 0) {
        // 加权平均：(旧均价 * 旧数量 + 新价格 * 新数量) / 总数量
        order.avgPx = (prevAvgPx * prevCumQty + fillPrice * fillQty) / order.cumQty;
    }
    
    // 更新订单状态
    if (order.leavesQty == 0) {
        order.status = OrderStatus::FILLED;
    } else {
        order.status = OrderStatus::PARTIALLY_FILLED;
    }
    
    order.updateTime = std::chrono::system_clock::now();
    
    // =========================================================================
    // 更新账户和持仓（如果设置了管理器）
    // =========================================================================
    auto sessionIt = orderSessionMap_.find(order.clOrdID);
    auto userIt = orderUserMap_.find(order.clOrdID);
    std::string accountId;
    if (userIt != orderUserMap_.end()) {
        accountId = userIt->second;  // 使用真实的用户ID
    }
    
    // 注意：持仓和保证金的处理由 SimulationApp::handleFill 统一处理
    // MatchingEngine 只负责撮合，不直接操作持仓
    // 这样可以正确处理开平仓逻辑（买入平空、卖出平多）
    
    // 发送 ExecutionReport
    if (sessionIt != orderSessionMap_.end()) {
        ExecutionReport report;
        report.orderID = order.orderID;
        report.clOrdID = order.clOrdID;
        report.execID = generateExecID();
        report.symbol = order.symbol;
        report.side = order.side;
        report.orderQty = order.orderQty;
        report.price = order.price;
        report.ordType = order.ordType;
        report.ordStatus = order.status;
        report.cumQty = order.cumQty;
        report.avgPx = order.avgPx;
        report.leavesQty = order.leavesQty;
        report.lastShares = fillQty;
        report.lastPx = fillPrice;
        report.transactTime = order.updateTime;
        report.execTransType = ExecTransType::NEW;
        
        LOG() << "[MatchingEngine] Order " << order.clOrdID << " filled: "
              << fillQty << " @ " << fillPrice
              << " (cumQty=" << order.cumQty << "/" << order.orderQty << ")";
        
        sendExecutionReport(sessionIt->second, report);
    }
}

void MatchingEngine::addToPendingOrders(const Order& order) {
    pendingOrders_[order.symbol].push_back(order);
    LOG() << "[MatchingEngine] Order " << order.clOrdID << " added to pending orders for " << order.symbol;
}

std::optional<Order> MatchingEngine::removeFromPendingOrders(const std::string& instrumentId, 
                                                              const std::string& clOrdID) {
    auto it = pendingOrders_.find(instrumentId);
    if (it == pendingOrders_.end()) {
        return std::nullopt;
    }
    
    auto& orders = it->second;
    for (auto orderIt = orders.begin(); orderIt != orders.end(); ++orderIt) {
        if (orderIt->clOrdID == clOrdID) {
            Order removedOrder = *orderIt;
            orders.erase(orderIt);
            return removedOrder;
        }
    }
    
    return std::nullopt;
}

} // namespace fix40
