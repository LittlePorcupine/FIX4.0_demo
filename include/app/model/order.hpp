/**
 * @file order.hpp
 * @brief 内部订单数据结构
 *
 * 定义与协议无关的订单表示，用于撮合引擎内部处理。
 * 与 FIX 消息解耦，便于支持多种协议接入。
 */

#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include "fix/application.hpp"

namespace fix40 {

// ============================================================================
// 枚举定义
// ============================================================================

/**
 * @enum OrderSide
 * @brief 买卖方向
 */
enum class OrderSide {
    BUY = 1,    ///< 买入
    SELL = 2    ///< 卖出
};

/**
 * @enum OrderType
 * @brief 订单类型
 */
enum class OrderType {
    MARKET = 1,  ///< 市价单
    LIMIT = 2    ///< 限价单
};

/**
 * @enum TimeInForce
 * @brief 订单有效期
 */
enum class TimeInForce {
    DAY = 0,    ///< 当日有效
    GTC = 1,    ///< 撤销前有效 (Good Till Cancel)
    IOC = 3,    ///< 立即成交否则取消 (Immediate Or Cancel)
    FOK = 4     ///< 全部成交否则取消 (Fill Or Kill)
};

/**
 * @enum OrderStatus
 * @brief 订单状态
 * 
 * @note 枚举值与 FIX 协议 OrdStatus(39) 标准定义保持一致，
 *       数值不连续是故意的，便于与协议对照和日志分析。
 */
enum class OrderStatus {
    NEW = 0,              ///< 新订单（已接受）
    PARTIALLY_FILLED = 1, ///< 部分成交
    FILLED = 2,           ///< 全部成交
    CANCELED = 4,         ///< 已撤销
    REJECTED = 8,         ///< 已拒绝
    PENDING_NEW = 10,     ///< 待确认（内部状态）
    PENDING_CANCEL = 6    ///< 待撤销（内部状态）
};

/**
 * @enum ExecTransType
 * @brief 执行事务类型 (FIX 4.0)
 */
enum class ExecTransType {
    NEW = 0,      ///< 新执行报告
    CANCEL = 1,   ///< 取消之前的执行报告
    CORRECT = 2,  ///< 更正之前的执行报告
    STATUS = 3    ///< 状态查询响应
};

// ============================================================================
// 订单结构
// ============================================================================

/**
 * @struct Order
 * @brief 内部订单表示
 *
 * 与协议无关的订单数据结构，包含订单的所有业务属性。
 * 撮合引擎只处理此结构，不直接接触 FIX 消息。
 */
struct Order {
    // -------------------------------------------------------------------------
    // 标识符
    // -------------------------------------------------------------------------
    std::string clOrdID;       ///< 客户端订单ID（客户端生成）
    std::string orderID;       ///< 服务端订单ID（撮合引擎生成）
    SessionID sessionID;       ///< 来源会话

    // -------------------------------------------------------------------------
    // 订单参数
    // -------------------------------------------------------------------------
    std::string symbol;        ///< 标的代码
    OrderSide side;            ///< 买卖方向
    OrderType ordType;         ///< 订单类型
    TimeInForce timeInForce;   ///< 有效期类型
    int64_t orderQty;          ///< 订单数量
    double price;              ///< 限价（市价单为 0）

    // -------------------------------------------------------------------------
    // 执行状态
    // -------------------------------------------------------------------------
    OrderStatus status;        ///< 当前状态
    int64_t cumQty;            ///< 累计成交数量
    int64_t leavesQty;         ///< 剩余数量
    double avgPx;              ///< 平均成交价

    // -------------------------------------------------------------------------
    // 时间戳
    // -------------------------------------------------------------------------
    std::chrono::system_clock::time_point createTime;  ///< 创建时间
    std::chrono::system_clock::time_point updateTime;  ///< 最后更新时间

    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     */
    Order()
        : side(OrderSide::BUY)
        , ordType(OrderType::LIMIT)
        , timeInForce(TimeInForce::DAY)
        , orderQty(0)
        , price(0.0)
        , status(OrderStatus::PENDING_NEW)
        , cumQty(0)
        , leavesQty(0)
        , avgPx(0.0)
        , createTime(std::chrono::system_clock::now())
        , updateTime(createTime)
    {}

    /**
     * @brief 计算剩余数量
     */
    void updateLeavesQty() {
        leavesQty = orderQty - cumQty;
    }

    /**
     * @brief 检查订单是否已完成（不可再成交）
     */
    bool isTerminal() const {
        return status == OrderStatus::FILLED ||
               status == OrderStatus::CANCELED ||
               status == OrderStatus::REJECTED;
    }

    /**
     * @brief 检查订单是否可撤销
     */
    bool isCancelable() const {
        return status == OrderStatus::NEW ||
               status == OrderStatus::PARTIALLY_FILLED;
    }
};

// ============================================================================
// 撤单请求
// ============================================================================

/**
 * @struct CancelRequest
 * @brief 撤单请求
 */
struct CancelRequest {
    std::string clOrdID;       ///< 本次撤单请求的ID
    std::string origClOrdID;   ///< 要撤销的原订单ID
    std::string symbol;        ///< 标的代码
    SessionID sessionID;       ///< 来源会话

    CancelRequest() = default;
};

// ============================================================================
// 执行报告
// ============================================================================

/**
 * @struct ExecutionReport
 * @brief 执行报告
 *
 * 用于向客户端报告订单状态变化和成交信息。
 */
struct ExecutionReport {
    // 标识符
    std::string orderID;       ///< 服务端订单ID
    std::string clOrdID;       ///< 客户端订单ID
    std::string execID;        ///< 执行ID（每次报告唯一）
    std::string origClOrdID;   ///< 原订单ID（撤单时使用）

    // 订单信息
    std::string symbol;        ///< 标的代码
    OrderSide side;            ///< 买卖方向
    OrderType ordType;         ///< 订单类型
    int64_t orderQty;          ///< 订单数量
    double price;              ///< 订单价格

    // 执行信息
    ExecTransType execTransType; ///< 执行事务类型
    OrderStatus ordStatus;       ///< 订单状态
    int64_t lastShares;          ///< 本次成交数量 (FIX 4.0: LastShares)
    double lastPx;               ///< 本次成交价格
    int64_t leavesQty;           ///< 剩余数量
    int64_t cumQty;              ///< 累计成交数量
    double avgPx;                ///< 平均成交价

    // 时间
    std::chrono::system_clock::time_point transactTime;  ///< 交易时间

    // 拒绝信息
    int ordRejReason;            ///< 拒绝原因代码
    std::string text;            ///< 文本说明

    // 会话信息
    SessionID sessionID;         ///< 目标会话

    /**
     * @brief 默认构造函数
     */
    ExecutionReport()
        : side(OrderSide::BUY)
        , ordType(OrderType::LIMIT)
        , orderQty(0)
        , price(0.0)
        , execTransType(ExecTransType::NEW)
        , ordStatus(OrderStatus::NEW)
        , lastShares(0)
        , lastPx(0.0)
        , leavesQty(0)
        , cumQty(0)
        , avgPx(0.0)
        , transactTime(std::chrono::system_clock::now())
        , ordRejReason(0)
    {}
};

} // namespace fix40
