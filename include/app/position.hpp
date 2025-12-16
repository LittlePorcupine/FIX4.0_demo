/**
 * @file position.hpp
 * @brief 持仓数据结构
 *
 * 定义模拟交易系统中的持仓数据结构，包含多空持仓、均价、盈亏等信息。
 * 用于追踪用户的合约头寸状态。
 */

#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace fix40 {

// ============================================================================
// 持仓结构
// ============================================================================

/**
 * @struct Position
 * @brief 合约持仓
 *
 * 用户在某个合约上的持仓信息，分为多头和空头两个方向。
 * 系统通过此结构追踪用户的头寸和盈亏状态。
 *
 * @par 盈亏计算
 * - 多头浮动盈亏 = (最新价 - 多头均价) × 多头持仓量 × 合约乘数
 * - 空头浮动盈亏 = (空头均价 - 最新价) × 空头持仓量 × 合约乘数
 *
 * @par 使用示例
 * @code
 * Position pos;
 * pos.accountId = "user001";
 * pos.instrumentId = "IF2601";
 * pos.longPosition = 2;
 * pos.longAvgPrice = 4000.0;
 * 
 * // 更新浮动盈亏（假设最新价4050，合约乘数300）
 * pos.updateProfit(4050.0, 300);
 * // 多头盈亏 = (4050 - 4000) * 2 * 300 = 30000
 * @endcode
 */
struct Position {
    // -------------------------------------------------------------------------
    // 标识符
    // -------------------------------------------------------------------------
    std::string accountId;      ///< 账户ID
    std::string instrumentId;   ///< 合约代码（如 "IF2601"）

    // -------------------------------------------------------------------------
    // 多头持仓
    // -------------------------------------------------------------------------
    int64_t longPosition;       ///< 多头持仓量（手数）
    double longAvgPrice;        ///< 多头持仓均价
    double longProfit;          ///< 多头浮动盈亏
    double longMargin;          ///< 多头占用保证金

    // -------------------------------------------------------------------------
    // 空头持仓
    // -------------------------------------------------------------------------
    int64_t shortPosition;      ///< 空头持仓量（手数）
    double shortAvgPrice;       ///< 空头持仓均价
    double shortProfit;         ///< 空头浮动盈亏
    double shortMargin;         ///< 空头占用保证金

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
    Position()
        : longPosition(0)
        , longAvgPrice(0.0)
        , longProfit(0.0)
        , longMargin(0.0)
        , shortPosition(0)
        , shortAvgPrice(0.0)
        , shortProfit(0.0)
        , shortMargin(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    /**
     * @brief 带标识符的构造函数
     *
     * @param accId 账户ID
     * @param instId 合约代码
     */
    Position(const std::string& accId, const std::string& instId)
        : accountId(accId)
        , instrumentId(instId)
        , longPosition(0)
        , longAvgPrice(0.0)
        , longProfit(0.0)
        , longMargin(0.0)
        , shortPosition(0)
        , shortAvgPrice(0.0)
        , shortProfit(0.0)
        , shortMargin(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    // -------------------------------------------------------------------------
    // 计算方法
    // -------------------------------------------------------------------------

    /**
     * @brief 更新浮动盈亏
     *
     * 根据最新价格重新计算多头和空头的浮动盈亏。
     *
     * @param lastPrice 最新价格
     * @param volumeMultiple 合约乘数
     *
     * @par 计算公式
     * - 多头盈亏 = (最新价 - 多头均价) × 多头持仓量 × 合约乘数
     * - 空头盈亏 = (空头均价 - 最新价) × 空头持仓量 × 合约乘数
     */
    void updateProfit(double lastPrice, int volumeMultiple) {
        longProfit = (lastPrice - longAvgPrice) * longPosition * volumeMultiple;
        shortProfit = (shortAvgPrice - lastPrice) * shortPosition * volumeMultiple;
    }

    /**
     * @brief 获取总浮动盈亏
     *
     * @return 多头盈亏 + 空头盈亏
     */
    double getTotalProfit() const {
        return longProfit + shortProfit;
    }

    /**
     * @brief 获取总持仓量
     *
     * @return 多头持仓 + 空头持仓
     */
    int64_t getTotalPosition() const {
        return longPosition + shortPosition;
    }

    /**
     * @brief 获取总占用保证金
     *
     * @return 多头保证金 + 空头保证金
     */
    double getTotalMargin() const {
        return longMargin + shortMargin;
    }

    /**
     * @brief 检查是否有持仓
     *
     * @return 如果有任意方向的持仓返回 true
     */
    bool hasPosition() const {
        return longPosition > 0 || shortPosition > 0;
    }

    /**
     * @brief 获取净持仓
     *
     * @return 多头持仓 - 空头持仓（正数表示净多，负数表示净空）
     */
    int64_t getNetPosition() const {
        return longPosition - shortPosition;
    }

    // -------------------------------------------------------------------------
    // 比较操作符（用于测试）
    // -------------------------------------------------------------------------

    /**
     * @brief 相等比较操作符
     *
     * 比较两个持仓的所有字段是否相等（不包括时间戳）。
     * 主要用于属性测试中的 round-trip 验证。
     *
     * @param other 另一个持仓
     * @return 如果所有字段相等则返回 true
     *
     * @note 此操作符使用精确比较，适用于序列化/反序列化的 round-trip 测试。
     *       如需比较计算结果，请使用带容差的比较方法。
     */
    bool operator==(const Position& other) const {
        return accountId == other.accountId &&
               instrumentId == other.instrumentId &&
               longPosition == other.longPosition &&
               longAvgPrice == other.longAvgPrice &&
               longProfit == other.longProfit &&
               longMargin == other.longMargin &&
               shortPosition == other.shortPosition &&
               shortAvgPrice == other.shortAvgPrice &&
               shortProfit == other.shortProfit &&
               shortMargin == other.shortMargin;
    }

    /**
     * @brief 不等比较操作符
     *
     * @param other 另一个持仓
     * @return 如果任意字段不相等则返回 true
     */
    bool operator!=(const Position& other) const {
        return !(*this == other);
    }
};

} // namespace fix40
