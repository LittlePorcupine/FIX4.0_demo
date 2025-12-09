/**
 * @file order_event.hpp
 * @brief 订单事件定义
 *
 * 定义从 Application 传递到撮合引擎的订单事件结构。
 */

#pragma once

#include <string>
#include "fix/fix_codec.hpp"
#include "fix/application.hpp"

namespace fix40 {

/**
 * @enum OrderEventType
 * @brief 订单事件类型
 */
enum class OrderEventType {
    NEW_ORDER,      ///< 新订单 (D)
    CANCEL_REQUEST, ///< 撤单请求 (F)
    REPLACE_REQUEST,///< 改单请求 (G)
    SESSION_LOGON,  ///< 会话登录
    SESSION_LOGOUT  ///< 会话登出
};

/**
 * @struct OrderEvent
 * @brief 订单事件
 *
 * 封装从 Application 回调传递到撮合引擎的事件数据。
 * 设计为可复制的值类型，便于在无锁队列中传递。
 */
struct OrderEvent {
    OrderEventType type;       ///< 事件类型
    SessionID sessionID;       ///< 来源会话标识
    FixMessage message;        ///< 原始 FIX 消息（仅订单事件有效）

    /**
     * @brief 构造订单事件
     */
    OrderEvent(OrderEventType t, const SessionID& sid, const FixMessage& msg = FixMessage())
        : type(t), sessionID(sid), message(msg) {}

    /**
     * @brief 默认构造函数
     */
    OrderEvent() : type(OrderEventType::NEW_ORDER) {}
};

} // namespace fix40
