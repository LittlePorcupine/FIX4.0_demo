/**
 * @file market_data_snapshot.hpp
 * @brief 行情快照数据结构
 *
 * 定义用于撮合判断的行情快照数据结构，包含买卖盘口信息。
 * 撮合引擎使用此结构判断订单是否可以成交。
 */

#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace fix40 {

// ============================================================================
// 行情快照结构
// ============================================================================

/**
 * @struct MarketDataSnapshot
 * @brief 合约行情快照
 *
 * 存储合约的实时行情数据，用于撮合引擎判断订单成交条件。
 * 数据来源于 CTP 行情推送，经过转换后存储在此结构中。
 *
 * @par 撮合规则
 * - 买单成交条件：买价 >= 卖一价（askPrice1）
 * - 卖单成交条件：卖价 <= 买一价（bidPrice1）
 *
 * @par 使用示例
 * @code
 * MarketDataSnapshot snapshot;
 * snapshot.instrumentId = "IF2601";
 * snapshot.lastPrice = 4000.0;
 * snapshot.bidPrice1 = 3999.8;
 * snapshot.bidVolume1 = 100;
 * snapshot.askPrice1 = 4000.2;
 * snapshot.askVolume1 = 50;
 * 
 * if (snapshot.isValid()) {
 *     // 可以用于撮合判断
 * }
 * @endcode
 */
struct MarketDataSnapshot {
    // -------------------------------------------------------------------------
    // 标识符
    // -------------------------------------------------------------------------
    std::string instrumentId;   ///< 合约代码（如 "IF2601"）

    // -------------------------------------------------------------------------
    // 价格信息
    // -------------------------------------------------------------------------
    double lastPrice;           ///< 最新价
    double bidPrice1;           ///< 买一价（最高买价）
    int32_t bidVolume1;         ///< 买一量
    double askPrice1;           ///< 卖一价（最低卖价）
    int32_t askVolume1;         ///< 卖一量

    // -------------------------------------------------------------------------
    // 涨跌停价格
    // -------------------------------------------------------------------------
    double upperLimitPrice;     ///< 涨停价
    double lowerLimitPrice;     ///< 跌停价

    // -------------------------------------------------------------------------
    // 时间戳
    // -------------------------------------------------------------------------
    std::chrono::system_clock::time_point updateTime;  ///< 行情更新时间

    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     *
     * 初始化所有数值字段为 0，时间戳为当前时间。
     */
    MarketDataSnapshot()
        : lastPrice(0.0)
        , bidPrice1(0.0)
        , bidVolume1(0)
        , askPrice1(0.0)
        , askVolume1(0)
        , upperLimitPrice(0.0)
        , lowerLimitPrice(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    /**
     * @brief 带合约代码的构造函数
     *
     * @param instId 合约代码
     */
    explicit MarketDataSnapshot(const std::string& instId)
        : instrumentId(instId)
        , lastPrice(0.0)
        , bidPrice1(0.0)
        , bidVolume1(0)
        , askPrice1(0.0)
        , askVolume1(0)
        , upperLimitPrice(0.0)
        , lowerLimitPrice(0.0)
        , updateTime(std::chrono::system_clock::now())
    {}

    // -------------------------------------------------------------------------
    // 验证方法
    // -------------------------------------------------------------------------

    /**
     * @brief 检查行情是否有效
     *
     * 行情有效的条件是买一价或卖一价大于 0。
     * 无效行情不能用于撮合判断。
     *
     * @return 如果行情有效返回 true
     */
    bool isValid() const {
        return bidPrice1 > 0 || askPrice1 > 0;
    }

    /**
     * @brief 检查是否有买盘
     *
     * @return 如果买一价和买一量都大于 0 返回 true
     */
    bool hasBid() const {
        return bidPrice1 > 0 && bidVolume1 > 0;
    }

    /**
     * @brief 检查是否有卖盘
     *
     * @return 如果卖一价和卖一量都大于 0 返回 true
     */
    bool hasAsk() const {
        return askPrice1 > 0 && askVolume1 > 0;
    }

    /**
     * @brief 获取买卖价差
     *
     * @return 卖一价 - 买一价，如果任一价格无效则返回 0
     */
    double getSpread() const {
        if (bidPrice1 > 0 && askPrice1 > 0) {
            return askPrice1 - bidPrice1;
        }
        return 0.0;
    }

    /**
     * @brief 获取中间价
     *
     * @return (买一价 + 卖一价) / 2，如果任一价格无效则返回最新价
     */
    double getMidPrice() const {
        if (bidPrice1 > 0 && askPrice1 > 0) {
            return (bidPrice1 + askPrice1) / 2.0;
        }
        return lastPrice;
    }

    // -------------------------------------------------------------------------
    // 比较操作符（用于测试）
    // -------------------------------------------------------------------------

    /**
     * @brief 相等比较操作符
     *
     * 比较两个行情快照的所有字段是否相等（不包括时间戳）。
     *
     * @param other 另一个行情快照
     * @return 如果所有字段相等则返回 true
     */
    bool operator==(const MarketDataSnapshot& other) const {
        return instrumentId == other.instrumentId &&
               lastPrice == other.lastPrice &&
               bidPrice1 == other.bidPrice1 &&
               bidVolume1 == other.bidVolume1 &&
               askPrice1 == other.askPrice1 &&
               askVolume1 == other.askVolume1 &&
               upperLimitPrice == other.upperLimitPrice &&
               lowerLimitPrice == other.lowerLimitPrice;
    }

    /**
     * @brief 不等比较操作符
     *
     * @param other 另一个行情快照
     * @return 如果任意字段不相等则返回 true
     */
    bool operator!=(const MarketDataSnapshot& other) const {
        return !(*this == other);
    }
};

} // namespace fix40
