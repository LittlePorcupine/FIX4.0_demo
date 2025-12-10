/**
 * @file matching_engine.cpp
 * @brief 撮合引擎实现
 */

#include "app/matching_engine.hpp"
#include "base/logger.hpp"

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
    const Order* order = event.getOrder();
    if (!order) {
        LOG() << "[MatchingEngine] Invalid NEW_ORDER event: no order data";
        return;
    }
    
    LOG() << "[MatchingEngine] Processing NewOrderSingle from " << event.sessionID.to_string();
    LOG() << "  ClOrdID: " << order->clOrdID;
    LOG() << "  Symbol: " << order->symbol;
    LOG() << "  Side: " << sideToString(order->side);
    LOG() << "  OrderQty: " << order->orderQty;
    LOG() << "  Price: " << order->price;
    LOG() << "  OrdType: " << ordTypeToString(order->ordType);
    LOG() << "  TimeInForce: " << tifToString(order->timeInForce);
    
    // TODO: 实现实际的撮合逻辑
    // 1. 生成 OrderID
    // 2. 验证订单参数
    // 3. 风控检查
    // 4. 订单簿匹配
    // 5. 生成 ExecutionReport 并发送回客户端
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
    
    // TODO: 实现实际的撤单逻辑
    // 1. 查找原订单
    // 2. 检查是否可撤
    // 3. 执行撤单
    // 4. 生成 ExecutionReport 或 OrderCancelReject
}

void MatchingEngine::handle_session_logon(const OrderEvent& event) {
    LOG() << "[MatchingEngine] Session logged on: " << event.sessionID.to_string();
    // TODO: 初始化该会话的交易状态
}

void MatchingEngine::handle_session_logout(const OrderEvent& event) {
    LOG() << "[MatchingEngine] Session logged out: " << event.sessionID.to_string();
    // TODO: 清理该会话的未完成订单
}

} // namespace fix40
