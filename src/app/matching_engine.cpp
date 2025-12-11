/**
 * @file matching_engine.cpp
 * @brief 撮合引擎实现
 */

#include "app/matching_engine.hpp"
#include "base/logger.hpp"
#include <sstream>
#include <iomanip>

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

const char* statusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING_NEW: return "PendingNew";
        case OrderStatus::NEW: return "New";
        case OrderStatus::PARTIALLY_FILLED: return "PartiallyFilled";
        case OrderStatus::FILLED: return "Filled";
        case OrderStatus::CANCELED: return "Canceled";
        case OrderStatus::PENDING_CANCEL: return "PendingCancel";
        case OrderStatus::REJECTED: return "Rejected";
        default: return "Unknown";
    }
}

} // anonymous namespace

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
        OrderEvent event;
        
        // 阻塞等待事件，带超时避免无法退出
        if (event_queue_.wait_dequeue_timed(event, std::chrono::milliseconds(100))) {
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
    
    // 复制订单（因为 addOrder 会修改订单）
    Order order = *orderPtr;
    
    LOG() << "[MatchingEngine] Processing NewOrderSingle from " << event.sessionID.to_string();
    LOG() << "  ClOrdID: " << order.clOrdID;
    LOG() << "  Symbol: " << order.symbol;
    LOG() << "  Side: " << sideToString(order.side);
    LOG() << "  OrderQty: " << order.orderQty;
    LOG() << "  Price: " << order.price;
    LOG() << "  OrdType: " << ordTypeToString(order.ordType);
    LOG() << "  TimeInForce: " << tifToString(order.timeInForce);
    
    // 记录订单与会话的映射
    orderSessionMap_[order.clOrdID] = event.sessionID;
    
    // 获取或创建订单簿
    OrderBook& book = getOrCreateOrderBook(order.symbol);
    
    // 执行撮合
    std::vector<Trade> trades = book.addOrder(order);
    
    // 发送订单确认（NEW 或 FILLED）
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
    report.transactTime = order.updateTime;
    
    if (trades.empty()) {
        // 无成交，发送 NEW 确认
        report.execTransType = ExecTransType::NEW;
        LOG() << "[MatchingEngine] Order " << order.clOrdID << " acknowledged: " << statusToString(order.status);
    } else {
        // 有成交
        report.execTransType = ExecTransType::NEW;  // FIX 4.0 用 NEW + OrdStatus 表示成交
        report.lastShares = trades.back().qty;
        report.lastPx = trades.back().price;
        LOG() << "[MatchingEngine] Order " << order.clOrdID << " executed: " 
              << order.cumQty << "/" << order.orderQty << " @ " << order.avgPx;
    }
    
    sendExecutionReport(event.sessionID, report);
    
    // 为每笔成交发送对手方的 ExecutionReport
    for (const auto& trade : trades) {
        // 确定对手方信息（从 Trade 中获取完整快照）
        bool isBuyer = (trade.buyClOrdID == order.clOrdID);
        const std::string& counterClOrdID = isBuyer ? trade.sellClOrdID : trade.buyClOrdID;
        
        auto it = orderSessionMap_.find(counterClOrdID);
        if (it != orderSessionMap_.end()) {
            ExecutionReport counterReport;
            counterReport.execID = generateExecID();
            counterReport.clOrdID = counterClOrdID;
            counterReport.symbol = trade.symbol;
            counterReport.lastShares = trade.qty;
            counterReport.lastPx = trade.price;
            counterReport.transactTime = trade.timestamp;
            counterReport.execTransType = ExecTransType::NEW;
            
            // 从 Trade 快照中获取完整的对手方订单信息
            if (isBuyer) {
                // 对手方是卖方
                counterReport.orderID = trade.sellOrderID;
                counterReport.side = OrderSide::SELL;
                counterReport.orderQty = trade.sellOrderQty;
                counterReport.price = trade.sellPrice;
                counterReport.ordType = trade.sellOrdType;
                counterReport.ordStatus = trade.sellStatus;
                counterReport.cumQty = trade.sellCumQty;
                counterReport.avgPx = trade.sellAvgPx;
                counterReport.leavesQty = trade.sellLeavesQty;
            } else {
                // 对手方是买方
                counterReport.orderID = trade.buyOrderID;
                counterReport.side = OrderSide::BUY;
                counterReport.orderQty = trade.buyOrderQty;
                counterReport.price = trade.buyPrice;
                counterReport.ordType = trade.buyOrdType;
                counterReport.ordStatus = trade.buyStatus;
                counterReport.cumQty = trade.buyCumQty;
                counterReport.avgPx = trade.buyAvgPx;
                counterReport.leavesQty = trade.buyLeavesQty;
            }
            
            sendExecutionReport(it->second, counterReport);
            
            // 如果对手方订单已完全成交，清理映射
            if (counterReport.ordStatus == OrderStatus::FILLED) {
                orderSessionMap_.erase(counterClOrdID);
            }
        }
    }
    
    // 如果订单已完全成交，清理映射
    if (order.isTerminal()) {
        orderSessionMap_.erase(order.clOrdID);
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
    
    // 查找订单簿
    auto bookIt = orderBooks_.find(req->symbol);
    if (bookIt == orderBooks_.end()) {
        LOG() << "[MatchingEngine] Cancel rejected: symbol " << req->symbol << " not found";
        // TODO: 发送 OrderCancelReject
        return;
    }
    
    // 执行撤单
    auto canceledOrder = bookIt->second->cancelOrder(req->origClOrdID);
    
    ExecutionReport report;
    report.clOrdID = req->clOrdID;
    report.origClOrdID = req->origClOrdID;
    report.execID = generateExecID();
    report.symbol = req->symbol;
    report.transactTime = std::chrono::system_clock::now();
    
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
        orderSessionMap_.erase(req->origClOrdID);
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

} // namespace fix40
