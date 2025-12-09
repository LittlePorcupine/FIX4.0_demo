/**
 * @file matching_engine.cpp
 * @brief 撮合引擎实现
 */

#include "app/matching_engine.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"

namespace fix40 {

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
    const auto& msg = event.message;
    
    LOG() << "[MatchingEngine] Processing NewOrderSingle from " << event.sessionID.to_string();
    
    // 提取订单字段
    if (msg.has(11)) {
        LOG() << "  ClOrdID: " << msg.get_string(11);
    }
    if (msg.has(55)) {
        LOG() << "  Symbol: " << msg.get_string(55);
    }
    if (msg.has(54)) {
        std::string side = msg.get_string(54);
        LOG() << "  Side: " << (side == "1" ? "Buy" : (side == "2" ? "Sell" : side));
    }
    if (msg.has(38)) {
        LOG() << "  OrderQty: " << msg.get_string(38);
    }
    if (msg.has(44)) {
        LOG() << "  Price: " << msg.get_string(44);
    }
    if (msg.has(40)) {
        std::string ord_type = msg.get_string(40);
        LOG() << "  OrdType: " << (ord_type == "1" ? "Market" : (ord_type == "2" ? "Limit" : ord_type));
    }
    
    // TODO: 实现实际的撮合逻辑
    // 1. 验证订单参数
    // 2. 风控检查
    // 3. 订单簿匹配
    // 4. 生成 ExecutionReport 并发送回客户端
}

void MatchingEngine::handle_cancel_request(const OrderEvent& event) {
    const auto& msg = event.message;
    
    LOG() << "[MatchingEngine] Processing OrderCancelRequest from " << event.sessionID.to_string();
    
    if (msg.has(41)) {
        LOG() << "  OrigClOrdID: " << msg.get_string(41);
    }
    if (msg.has(11)) {
        LOG() << "  ClOrdID: " << msg.get_string(11);
    }
    if (msg.has(55)) {
        LOG() << "  Symbol: " << msg.get_string(55);
    }
    
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
