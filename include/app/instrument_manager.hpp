/**
 * @file instrument_manager.hpp
 * @brief 合约信息管理模块
 *
 * 提供合约信息的加载、查询和更新功能。
 * 支持从配置文件加载合约静态信息，并从行情更新涨跌停价格。
 */

#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <vector>
#include "app/instrument.hpp"

namespace fix40 {

/**
 * @class InstrumentManager
 * @brief 合约信息管理器
 *
 * 负责合约信息的加载、查询和更新操作。
 * 系统启动时从配置文件加载合约静态信息，运行时从行情更新涨跌停价格。
 *
 * @par 线程安全
 * 所有公共方法都是线程安全的，使用互斥锁保护内部数据。
 *
 * @par 使用示例
 * @code
 * InstrumentManager mgr;
 * 
 * // 从配置文件加载合约
 * mgr.loadFromConfig("config/instruments.json");
 * 
 * // 查询合约信息
 * const Instrument* inst = mgr.getInstrument("IF2601");
 * if (inst) {
 *     double margin = inst->calculateMargin(4000.0, 2);
 * }
 * 
 * // 从行情更新涨跌停价格
 * mgr.updateLimitPrices("IF2601", 4400.0, 3600.0);
 * @endcode
 */
class InstrumentManager {
public:
    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     */
    InstrumentManager() = default;

    /**
     * @brief 析构函数
     */
    ~InstrumentManager() = default;

    // 禁用拷贝
    InstrumentManager(const InstrumentManager&) = delete;
    InstrumentManager& operator=(const InstrumentManager&) = delete;

    // -------------------------------------------------------------------------
    // 加载方法
    // -------------------------------------------------------------------------

    /**
     * @brief 从配置文件加载合约信息
     *
     * 解析JSON格式的配置文件，加载合约静态信息。
     *
     * @param configPath 配置文件路径
     * @return 加载成功返回 true，失败返回 false
     *
     * @par 配置文件格式
     * @code
     * {
     *   "instruments": [
     *     {
     *       "instrumentId": "IF2601",
     *       "exchangeId": "CFFEX",
     *       "productId": "IF",
     *       "priceTick": 0.2,
     *       "volumeMultiple": 300,
     *       "marginRate": 0.12
     *     }
     *   ]
     * }
     * @endcode
     */
    bool loadFromConfig(const std::string& configPath);

    /**
     * @brief 添加单个合约
     *
     * 手动添加合约信息，主要用于测试。
     *
     * @param instrument 合约信息
     */
    void addInstrument(const Instrument& instrument);

    /**
     * @brief 批量添加合约
     *
     * @param instruments 合约列表
     */
    void addInstruments(const std::vector<Instrument>& instruments);

    // -------------------------------------------------------------------------
    // 查询方法
    // -------------------------------------------------------------------------

    /**
     * @brief 获取合约信息
     *
     * @param instrumentId 合约代码
     * @return 合约信息指针，不存在时返回 nullptr
     *
     * @note 返回的指针在下次修改操作前有效
     */
    const Instrument* getInstrument(const std::string& instrumentId) const;

    /**
     * @brief 获取合约信息（可选类型）
     *
     * @param instrumentId 合约代码
     * @return 合约信息的副本，不存在时返回 std::nullopt
     */
    std::optional<Instrument> getInstrumentCopy(const std::string& instrumentId) const;

    /**
     * @brief 检查合约是否存在
     *
     * @param instrumentId 合约代码
     * @return 存在返回 true
     */
    bool hasInstrument(const std::string& instrumentId) const;

    /**
     * @brief 获取所有合约代码
     *
     * @return 合约代码列表
     */
    std::vector<std::string> getAllInstrumentIds() const;

    /**
     * @brief 获取合约数量
     *
     * @return 已加载的合约数量
     */
    size_t size() const;

    // -------------------------------------------------------------------------
    // 更新方法
    // -------------------------------------------------------------------------

    /**
     * @brief 更新涨跌停价格
     *
     * 从行情数据更新合约的涨跌停价格。
     *
     * @param instrumentId 合约代码
     * @param upperLimit 涨停价
     * @param lowerLimit 跌停价
     * @return 更新成功返回 true，合约不存在返回 false
     */
    bool updateLimitPrices(const std::string& instrumentId,
                           double upperLimit, double lowerLimit);

    /**
     * @brief 更新昨结算价
     *
     * @param instrumentId 合约代码
     * @param preSettlementPrice 昨结算价
     * @return 更新成功返回 true，合约不存在返回 false
     */
    bool updatePreSettlementPrice(const std::string& instrumentId,
                                   double preSettlementPrice);

    // -------------------------------------------------------------------------
    // 清理方法
    // -------------------------------------------------------------------------

    /**
     * @brief 清空所有合约
     */
    void clear();

private:
    /// 合约映射表：instrumentId -> Instrument
    std::unordered_map<std::string, Instrument> instruments_;

    /// 互斥锁，保护 instruments_
    mutable std::mutex mutex_;
};

} // namespace fix40
