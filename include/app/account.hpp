/**
 * @file account.hpp
 * @brief 虚拟交易账户数据结构
 *
 * 定义模拟交易系统中的账户数据结构，包含资金、保证金、盈亏等信息。
 * 用于追踪用户的虚拟资金状态。
 */

#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace fix40 {

// ============================================================================
// 账户结构
// ============================================================================

/**
 * @struct Account
 * @brief 虚拟交易账户
 *
 * 用户的虚拟资金账户，包含余额、可用资金、冻结保证金等信息。
 * 系统通过此结构追踪用户的资金状态和交易表现。
 *
 * @par 资金关系
 * - 可用资金 = 余额 + 持仓盈亏 - 冻结保证金 - 占用保证金
 * - 动态权益 = 余额 + 持仓盈亏
 * - 风险度 = 占用保证金 / 动态权益
 *
 * @par 使用示例
 * @code
 * Account account;
 * account.accountId = "user001";
 * account.balance = 1000000.0;
 * account.available = 1000000.0;
 * 
 * double equity = account.getDynamicEquity();
 * double risk = account.getRiskRatio();
 * @endcode
 */
struct Account {
    // -------------------------------------------------------------------------
    // 标识符
    // -------------------------------------------------------------------------
    std::string accountId;      ///< 账户ID（唯一标识）

    // -------------------------------------------------------------------------
    // 资金信息
    // -------------------------------------------------------------------------
    double balance;             ///< 账户余额（静态权益，初始资金 + 平仓盈亏）
    double available;           ///< 可用资金（可用于开仓的资金）
    double frozenMargin;        ///< 冻结保证金（挂单占用，尚未成交）
    double usedMargin;          ///< 占用保证金（持仓占用，已成交）

    // -------------------------------------------------------------------------
    // 盈亏信息
    // -------------------------------------------------------------------------
    double positionProfit;      ///< 持仓盈亏（浮动盈亏，根据最新价实时计算）
    double closeProfit;         ///< 平仓盈亏（已实现盈亏，累计值）

    // -------------------------------------------------------------------------
    // 时间戳
    // -------------------------------------------------------------------------
    std::chrono::system_clock::time_point updateTime;  ///< 最后更新时间

    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     *
     * 初始化所有数值字段为 0，时间戳为当前时间。
     */
    Account()
        : balance(0.0)
        , available(0.0)
        , frozenMargin(0.0)
        , usedMargin(0.0)
        , positionProfit(0.0)
        , closeProfit(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    /**
     * @brief 带初始余额的构造函数
     *
     * @param id 账户ID
     * @param initialBalance 初始余额
     */
    Account(const std::string& id, double initialBalance)
        : accountId(id)
        , balance(initialBalance)
        , available(initialBalance)
        , frozenMargin(0.0)
        , usedMargin(0.0)
        , positionProfit(0.0)
        , closeProfit(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    // -------------------------------------------------------------------------
    // 计算方法
    // -------------------------------------------------------------------------

    /**
     * @brief 计算动态权益
     *
     * 动态权益 = 余额 + 持仓盈亏
     * 反映账户的实时价值，包含未实现的浮动盈亏。
     *
     * @return 动态权益值
     */
    double getDynamicEquity() const {
        return balance + positionProfit;
    }

    /**
     * @brief 计算风险度
     *
     * 风险度 = 占用保证金 / 动态权益
     * 用于评估账户的风险水平，值越高风险越大。
     *
     * @return 风险度（0.0 ~ 1.0+），动态权益为0或负时返回0
     *
     * @note 当动态权益 <= 0 时返回 0，避免除零错误和负值风险度
     */
    double getRiskRatio() const {
        double equity = getDynamicEquity();
        return equity > 0 ? usedMargin / equity : 0.0;
    }

    /**
     * @brief 重新计算可用资金
     *
     * 可用资金 = 余额 + 持仓盈亏 - 冻结保证金 - 占用保证金
     * 在资金状态变化后调用此方法更新可用资金。
     */
    void recalculateAvailable() {
        available = balance + positionProfit - frozenMargin - usedMargin;
    }

    // -------------------------------------------------------------------------
    // 比较操作符（用于测试）
    // -------------------------------------------------------------------------

    /**
     * @brief 相等比较操作符
     *
     * 比较两个账户的所有字段是否相等（不包括时间戳）。
     * 主要用于属性测试中的 round-trip 验证。
     *
     * @param other 另一个账户
     * @return 如果所有字段相等则返回 true
     */
    bool operator==(const Account& other) const {
        return accountId == other.accountId &&
               balance == other.balance &&
               available == other.available &&
               frozenMargin == other.frozenMargin &&
               usedMargin == other.usedMargin &&
               positionProfit == other.positionProfit &&
               closeProfit == other.closeProfit;
    }

    /**
     * @brief 不等比较操作符
     *
     * @param other 另一个账户
     * @return 如果任意字段不相等则返回 true
     */
    bool operator!=(const Account& other) const {
        return !(*this == other);
    }
};

} // namespace fix40
