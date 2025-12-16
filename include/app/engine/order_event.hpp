/**
 * @file order_event.hpp
 * @brief 订单事件定义
 *
 * 定义从 Application 传递到撮合引擎的订单事件结构。
 * 事件使用内部 Order 结构，与 FIX 协议解耦。
 */

#pragma once

#include <string>
#include <variant>
#include "app/model/order.hpp"

namespace fix40 {

/**
 * @enum OrderEventType
 * @brief 订单事件类型
 */
enum class OrderEventType {
    NEW_ORDER,       ///< 新订单
    CANCEL_REQUEST,  ///< 撤单请求
    SESSION_LOGON,   ///< 会话登录
    SESSION_LOGOUT   ///< 会话登出
};

/**
 * @struct OrderEvent
 * @brief 订单事件
 *
 * 封装从 Application 回调传递到撮合引擎的事件数据。
 * 使用内部数据结构（Order/CancelRequest），与 FIX 协议解耦。
 *
 * @par 数据流
 * 1. SimulationApp::fromApp() 收到 FixMessage
 * 2. 解析 FixMessage，转换为 Order 或 CancelRequest
 * 3. 封装为 OrderEvent，push 到队列
 * 4. MatchingEngine 消费 OrderEvent，处理内部结构
 */
struct OrderEvent {
    OrderEventType type;       ///< 事件类型
    SessionID sessionID;       ///< 来源会话标识

    /// 事件数据（根据 type 使用不同类型）
    /// - NEW_ORDER: Order
    /// - CANCEL_REQUEST: CancelRequest
    /// - SESSION_LOGON/LOGOUT: std::monostate（无数据）
    std::variant<std::monostate, Order, CancelRequest> data;

    // =========================================================================
    // 构造函数
    // =========================================================================

    /**
     * @brief 默认构造函数
     */
    OrderEvent()
        : type(OrderEventType::SESSION_LOGON)
        , data(std::monostate{})
    {}

    /**
     * @brief 构造会话事件（登录/登出）
     */
    OrderEvent(OrderEventType t, const SessionID& sid)
        : type(t)
        , sessionID(sid)
        , data(std::monostate{})
    {}

    /**
     * @brief 构造新订单事件
     */
    static OrderEvent newOrder(const Order& order) {
        OrderEvent event;
        event.type = OrderEventType::NEW_ORDER;
        event.sessionID = order.sessionID;
        event.data = order;
        return event;
    }

    /**
     * @brief 构造撤单事件
     */
    static OrderEvent cancelRequest(const CancelRequest& req) {
        OrderEvent event;
        event.type = OrderEventType::CANCEL_REQUEST;
        event.sessionID = req.sessionID;
        event.data = req;
        return event;
    }

    // =========================================================================
    // 访问器
    // =========================================================================

    /**
     * @brief 获取订单数据（仅 NEW_ORDER 有效）
     * @return const Order* 订单指针，类型不匹配返回 nullptr
     */
    const Order* getOrder() const {
        return std::get_if<Order>(&data);
    }

    /**
     * @brief 获取撤单请求（仅 CANCEL_REQUEST 有效）
     * @return const CancelRequest* 撤单请求指针，类型不匹配返回 nullptr
     */
    const CancelRequest* getCancelRequest() const {
        return std::get_if<CancelRequest>(&data);
    }
};

} // namespace fix40
