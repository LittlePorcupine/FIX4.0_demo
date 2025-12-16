/**
 * @file risk_manager.hpp
 * @brief 风险控制模块
 *
 * 提供订单风控检查功能，包括资金检查、价格检查、持仓检查等。
 * 在订单提交到撮合引擎之前进行风险验证。
 */

#pragma once

#include <string>
#include <cstdint>
#include "app/order.hpp"
#include "app/account.hpp"
#include "app/position.hpp"
#include "app/instrument.hpp"
#include "app/market_data_snapshot.hpp"

namespace fix40 {

// ============================================================================
// 拒绝原因代码
// ============================================================================

/**
 * @enum RejectReason
 * @brief 订单拒绝原因代码
 *
 * 定义风控检查失败时的拒绝原因代码，与设计文档中的错误处理表对应。
 */
enum class RejectReason {
    NONE = 0,                   ///< 无错误（检查通过）
    INSTRUMENT_NOT_FOUND = 1,   ///< 合约不存在
    INSUFFICIENT_FUNDS = 2,     ///< 资金不足
    PRICE_OUT_OF_LIMIT = 3,     ///< 价格超限（超出涨跌停）
    INSUFFICIENT_POSITION = 4,  ///< 持仓不足（平仓数量超过持仓）
    NO_COUNTER_PARTY = 5,       ///< 无对手盘（市价单）
    ORDER_NOT_FOUND = 6         ///< 订单不存在（撤单时）
};

// ============================================================================
// 检查结果结构
// ============================================================================

/**
 * @struct CheckResult
 * @brief 风控检查结果
 *
 * 包含检查是否通过、拒绝原因代码和拒绝原因文本。
 */
struct CheckResult {
    bool passed;                ///< 检查是否通过
    RejectReason rejectReason;  ///< 拒绝原因代码
    std::string rejectText;     ///< 拒绝原因文本

    /**
     * @brief 默认构造函数（检查通过）
     */
    CheckResult()
        : passed(true)
        , rejectReason(RejectReason::NONE)
    {}

    /**
     * @brief 构造失败结果
     *
     * @param reason 拒绝原因代码
     * @param text 拒绝原因文本
     */
    CheckResult(RejectReason reason, const std::string& text)
        : passed(false)
        , rejectReason(reason)
        , rejectText(text)
    {}

    /**
     * @brief 创建成功结果
     *
     * @return 检查通过的结果
     */
    static CheckResult success() {
        return CheckResult();
    }

    /**
     * @brief 创建失败结果
     *
     * @param reason 拒绝原因代码
     * @param text 拒绝原因文本
     * @return 检查失败的结果
     */
    static CheckResult failure(RejectReason reason, const std::string& text) {
        return CheckResult(reason, text);
    }
};

// ============================================================================
// 开平标志
// ============================================================================

/**
 * @enum OffsetFlag
 * @brief 开平标志
 *
 * 用于区分开仓和平仓订单。
 */
enum class OffsetFlag {
    OPEN = 0,   ///< 开仓
    CLOSE = 1   ///< 平仓
};

// ============================================================================
// 风控管理器
// ============================================================================

/**
 * @class RiskManager
 * @brief 风险控制模块
 *
 * 负责订单提交前的风险检查，包括：
 * - 资金检查：验证可用资金是否足够支付保证金
 * - 价格检查：验证限价单价格是否在涨跌停范围内
 * - 持仓检查：验证平仓数量是否超过持仓
 *
 * @par 使用示例
 * @code
 * RiskManager riskMgr;
 * 
 * Order order;
 * order.symbol = "IF2601";
 * order.side = OrderSide::BUY;
 * order.ordType = OrderType::LIMIT;
 * order.price = 4000.0;
 * order.orderQty = 2;
 * 
 * Account account("user001", 1000000.0);
 * Position position("user001", "IF2601");
 * Instrument instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
 * instrument.updateLimitPrices(4200.0, 3800.0);
 * MarketDataSnapshot snapshot("IF2601");
 * snapshot.askPrice1 = 4000.2;
 * 
 * CheckResult result = riskMgr.checkOrder(order, account, position, instrument, snapshot, OffsetFlag::OPEN);
 * if (result.passed) {
 *     // 提交到撮合引擎
 * } else {
 *     // 拒绝订单，返回 result.rejectText
 * }
 * @endcode
 *
 * @par 线程安全
 * 所有公共方法都是线程安全的（无状态类）。
 */
class RiskManager {
public:
    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     */
    RiskManager() = default;

    // -------------------------------------------------------------------------
    // 主检查方法
    // -------------------------------------------------------------------------

    /**
     * @brief 订单风控检查
     *
     * 对订单进行完整的风控检查，包括资金、价格、持仓等。
     *
     * @param order 待检查的订单
     * @param account 账户信息
     * @param position 持仓信息
     * @param instrument 合约信息
     * @param snapshot 行情快照
     * @param offsetFlag 开平标志
     * @return 检查结果
     *
     * @par 检查顺序
     * 1. 价格检查（限价单）
     * 2. 资金检查（开仓）
     * 3. 持仓检查（平仓）
     * 4. 对手盘检查（市价单）
     */
    CheckResult checkOrder(
        const Order& order,
        const Account& account,
        const Position& position,
        const Instrument& instrument,
        const MarketDataSnapshot& snapshot,
        OffsetFlag offsetFlag
    ) const;

    // -------------------------------------------------------------------------
    // 子检查方法（公开以便单独测试）
    // -------------------------------------------------------------------------

    /**
     * @brief 检查可用资金是否足够
     *
     * 验证账户可用资金是否足够支付开仓所需的保证金。
     *
     * @param order 订单信息
     * @param account 账户信息
     * @param instrument 合约信息
     * @return 检查结果
     *
     * @par 计算公式
     * 所需保证金 = 价格 × 数量 × 合约乘数 × 保证金率
     *
     * @note 市价单使用涨停价（买单）或跌停价（卖单）计算保证金
     */
    CheckResult checkMargin(
        const Order& order,
        const Account& account,
        const Instrument& instrument
    ) const;

    /**
     * @brief 检查价格是否在涨跌停范围内
     *
     * 验证限价单的价格是否在当日涨跌停价格范围内。
     *
     * @param order 订单信息
     * @param instrument 合约信息
     * @return 检查结果
     *
     * @note 市价单不进行价格检查
     */
    CheckResult checkPrice(
        const Order& order,
        const Instrument& instrument
    ) const;

    /**
     * @brief 检查平仓数量是否超过持仓
     *
     * 验证平仓订单的数量是否超过当前持仓数量。
     *
     * @param order 订单信息
     * @param position 持仓信息
     * @return 检查结果
     *
     * @note 买单平空头持仓，卖单平多头持仓
     */
    CheckResult checkPosition(
        const Order& order,
        const Position& position
    ) const;

    /**
     * @brief 检查市价单是否有对手盘
     *
     * 验证市价单是否有可成交的对手盘。
     *
     * @param order 订单信息
     * @param snapshot 行情快照
     * @return 检查结果
     *
     * @note 买单需要有卖盘，卖单需要有买盘
     */
    CheckResult checkCounterParty(
        const Order& order,
        const MarketDataSnapshot& snapshot
    ) const;

    // -------------------------------------------------------------------------
    // 辅助方法
    // -------------------------------------------------------------------------

    /**
     * @brief 计算订单所需保证金
     *
     * @param order 订单信息
     * @param instrument 合约信息
     * @return 所需保证金金额
     *
     * @note 市价单使用涨停价（买单）或跌停价（卖单）计算
     */
    double calculateRequiredMargin(
        const Order& order,
        const Instrument& instrument
    ) const;
};

} // namespace fix40
