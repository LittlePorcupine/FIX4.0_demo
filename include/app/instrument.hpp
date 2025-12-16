/**
 * @file instrument.hpp
 * @brief 合约信息数据结构
 *
 * 定义期货合约的静态属性，如保证金率、合约乘数、涨跌停价等。
 * 用于交易规则验证和保证金计算。
 */

#pragma once

#include <string>
#include <cstdint>
#include <cmath>

namespace fix40 {

// ============================================================================
// 合约信息结构
// ============================================================================

/**
 * @struct Instrument
 * @brief 合约静态信息
 *
 * 期货合约的基本属性，包含交易规则相关的参数。
 * 系统通过此结构进行保证金计算和价格有效性检查。
 *
 * @par 保证金计算
 * 保证金 = 价格 × 数量 × 合约乘数 × 保证金率
 *
 * @par 使用示例
 * @code
 * Instrument inst;
 * inst.instrumentId = "IF2601";
 * inst.exchangeId = "CFFEX";
 * inst.volumeMultiple = 300;
 * inst.marginRate = 0.12;  // 12%
 * inst.priceTick = 0.2;
 * 
 * // 计算开仓保证金
 * double margin = inst.calculateMargin(4000.0, 2);
 * // margin = 4000 * 2 * 300 * 0.12 = 288000
 * 
 * // 检查价格有效性
 * bool valid = inst.isPriceValid(4050.0);
 * @endcode
 */
struct Instrument {
    // -------------------------------------------------------------------------
    // 标识符
    // -------------------------------------------------------------------------
    std::string instrumentId;   ///< 合约代码（如 "IF2601"）
    std::string exchangeId;     ///< 交易所代码（如 "CFFEX"）
    std::string productId;      ///< 品种代码（如 "IF"）

    // -------------------------------------------------------------------------
    // 交易参数
    // -------------------------------------------------------------------------
    double priceTick;           ///< 最小变动价位（如 0.2）
    int volumeMultiple;         ///< 合约乘数（如 300）
    double marginRate;          ///< 保证金率（如 0.12 表示 12%）

    // -------------------------------------------------------------------------
    // 价格限制（从行情更新）
    // -------------------------------------------------------------------------
    double upperLimitPrice;     ///< 涨停价
    double lowerLimitPrice;     ///< 跌停价
    double preSettlementPrice;  ///< 昨结算价

    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     *
     * 初始化所有数值字段为 0。
     */
    Instrument()
        : priceTick(0.0)
        , volumeMultiple(0)
        , marginRate(0.0)
        , upperLimitPrice(0.0)
        , lowerLimitPrice(0.0)
        , preSettlementPrice(0.0)
    {}

    /**
     * @brief 带基本参数的构造函数
     *
     * @param instId 合约代码
     * @param exchId 交易所代码
     * @param prodId 品种代码
     * @param tick 最小变动价位
     * @param multiple 合约乘数
     * @param margin 保证金率
     */
    Instrument(const std::string& instId, const std::string& exchId,
               const std::string& prodId, double tick, int multiple, double margin)
        : instrumentId(instId)
        , exchangeId(exchId)
        , productId(prodId)
        , priceTick(tick)
        , volumeMultiple(multiple)
        , marginRate(margin)
        , upperLimitPrice(0.0)
        , lowerLimitPrice(0.0)
        , preSettlementPrice(0.0)
    {}

    // -------------------------------------------------------------------------
    // 计算方法
    // -------------------------------------------------------------------------

    /**
     * @brief 计算开仓保证金
     *
     * 根据价格和数量计算所需的保证金。
     *
     * @param price 开仓价格
     * @param volume 开仓数量（手数）
     * @return 所需保证金金额
     *
     * @par 计算公式
     * 保证金 = 价格 × 数量 × 合约乘数 × 保证金率
     */
    double calculateMargin(double price, int64_t volume) const {
        return price * volume * volumeMultiple * marginRate;
    }

    /**
     * @brief 检查价格是否在涨跌停范围内
     *
     * 验证给定价格是否在当日涨跌停价格范围内。
     *
     * @param price 待检查的价格
     * @return 如果价格在有效范围内返回 true
     *
     * @note 如果涨跌停价格未设置（为0），则认为价格有效
     */
    bool isPriceValid(double price) const {
        // 如果涨跌停价格未设置，认为价格有效
        if (lowerLimitPrice <= 0 && upperLimitPrice <= 0) {
            return true;
        }
        return price >= lowerLimitPrice && price <= upperLimitPrice;
    }

    /**
     * @brief 检查价格是否符合最小变动价位
     *
     * 验证给定价格是否是最小变动价位的整数倍。
     *
     * @param price 待检查的价格
     * @return 如果价格符合最小变动价位返回 true
     *
     * @note 使用容差比较以处理浮点数精度问题
     */
    bool isPriceTickValid(double price) const {
        if (priceTick <= 0) {
            return true;
        }
        double remainder = std::fmod(price, priceTick);
        // 使用容差比较
        return remainder < 1e-9 || (priceTick - remainder) < 1e-9;
    }

    /**
     * @brief 更新涨跌停价格
     *
     * 从行情数据更新涨跌停价格。
     *
     * @param upper 涨停价
     * @param lower 跌停价
     */
    void updateLimitPrices(double upper, double lower) {
        upperLimitPrice = upper;
        lowerLimitPrice = lower;
    }

    // -------------------------------------------------------------------------
    // 比较操作符（用于测试）
    // -------------------------------------------------------------------------

    /**
     * @brief 相等比较操作符
     *
     * 比较两个合约的所有字段是否相等。
     *
     * @param other 另一个合约
     * @return 如果所有字段相等则返回 true
     *
     * @note 此操作符使用精确比较，适用于序列化/反序列化的 round-trip 测试。
     *       如需比较计算结果，请使用带容差的比较方法。
     */
    bool operator==(const Instrument& other) const {
        return instrumentId == other.instrumentId &&
               exchangeId == other.exchangeId &&
               productId == other.productId &&
               priceTick == other.priceTick &&
               volumeMultiple == other.volumeMultiple &&
               marginRate == other.marginRate &&
               upperLimitPrice == other.upperLimitPrice &&
               lowerLimitPrice == other.lowerLimitPrice &&
               preSettlementPrice == other.preSettlementPrice;
    }

    /**
     * @brief 不等比较操作符
     *
     * @param other 另一个合约
     * @return 如果任意字段不相等则返回 true
     */
    bool operator!=(const Instrument& other) const {
        return !(*this == other);
    }
};

} // namespace fix40
