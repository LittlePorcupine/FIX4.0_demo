/**
 * @file position_manager.hpp
 * @brief 持仓管理模块
 *
 * 提供持仓的开仓、平仓、盈亏计算等功能。
 * 支持与存储层集成进行持久化。
 */

#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <vector>
#include "app/position.hpp"
#include "app/order.hpp"
#include "app/market_data_snapshot.hpp"

namespace fix40 {

// 前向声明
class IStore;

/**
 * @class PositionManager
 * @brief 持仓管理器
 *
 * 负责持仓的开仓、平仓、盈亏计算等操作。
 * 支持与IStore接口集成进行数据持久化。
 *
 * @par 线程安全
 * 所有公共方法都是线程安全的，使用互斥锁保护内部数据。
 *
 * @par 持仓计算
 * - 开仓：增加持仓量，计算新的持仓均价
 * - 平仓：减少持仓量，计算平仓盈亏
 * - 浮动盈亏：根据最新价实时计算
 *
 * @par 使用示例
 * @code
 * PositionManager mgr;
 * 
 * // 开仓
 * mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
 * 
 * // 更新浮动盈亏
 * MarketDataSnapshot snapshot;
 * snapshot.lastPrice = 4050.0;
 * mgr.updateAllProfits(snapshot, 300);
 * 
 * // 平仓
 * double profit = mgr.closePosition("user001", "IF2601", OrderSide::BUY, 1, 4050.0, 300);
 * @endcode
 */
class PositionManager {
public:
    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     */
    PositionManager();

    /**
     * @brief 带存储接口的构造函数
     *
     * @param store 存储接口指针（可为nullptr）
     */
    explicit PositionManager(IStore* store);

    /**
     * @brief 析构函数
     */
    ~PositionManager() = default;

    // 禁用拷贝
    PositionManager(const PositionManager&) = delete;
    PositionManager& operator=(const PositionManager&) = delete;

    // -------------------------------------------------------------------------
    // 查询方法
    // -------------------------------------------------------------------------

    /**
     * @brief 获取持仓
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @return 持仓信息，不存在时返回 std::nullopt
     */
    std::optional<Position> getPosition(const std::string& accountId,
                                         const std::string& instrumentId) const;

    /**
     * @brief 获取账户所有持仓
     *
     * @param accountId 账户ID
     * @return 持仓列表
     */
    std::vector<Position> getPositionsByAccount(const std::string& accountId) const;

    /**
     * @brief 获取所有持仓
     *
     * @return 所有持仓列表
     */
    std::vector<Position> getAllPositions() const;

    /**
     * @brief 检查是否有持仓
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @return 有持仓返回 true
     */
    bool hasPosition(const std::string& accountId, const std::string& instrumentId) const;

    /**
     * @brief 获取持仓数量
     *
     * @return 持仓记录数量
     */
    size_t size() const;

    // -------------------------------------------------------------------------
    // 开仓操作
    // -------------------------------------------------------------------------

    /**
     * @brief 开仓
     *
     * 增加持仓量，计算新的持仓均价。
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @param side 买卖方向（BUY=多头，SELL=空头）
     * @param volume 开仓数量
     * @param price 开仓价格
     * @param margin 占用保证金
     *
     * @par 均价计算
     * 新均价 = (原均价 × 原持仓 + 开仓价 × 开仓量) / (原持仓 + 开仓量)
     */
    void openPosition(const std::string& accountId,
                      const std::string& instrumentId,
                      OrderSide side,
                      int64_t volume,
                      double price,
                      double margin);

    // -------------------------------------------------------------------------
    // 平仓操作
    // -------------------------------------------------------------------------

    /**
     * @brief 平仓
     *
     * 减少持仓量，计算平仓盈亏。
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @param side 平仓方向（BUY=平空头，SELL=平多头）
     * @param volume 平仓数量
     * @param price 平仓价格
     * @param volumeMultiple 合约乘数
     * @return 平仓盈亏
     *
     * @par 盈亏计算
     * - 平多头：(平仓价 - 持仓均价) × 平仓量 × 合约乘数
     * - 平空头：(持仓均价 - 平仓价) × 平仓量 × 合约乘数
     */
    double closePosition(const std::string& accountId,
                         const std::string& instrumentId,
                         OrderSide side,
                         int64_t volume,
                         double price,
                         int volumeMultiple);

    // -------------------------------------------------------------------------
    // 盈亏更新
    // -------------------------------------------------------------------------

    /**
     * @brief 更新指定合约所有持仓的浮动盈亏
     *
     * @param snapshot 行情快照
     * @param volumeMultiple 合约乘数
     */
    void updateAllProfits(const MarketDataSnapshot& snapshot, int volumeMultiple);

    /**
     * @brief 更新指定持仓的浮动盈亏
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @param lastPrice 最新价
     * @param volumeMultiple 合约乘数
     * @return 更新后的总浮动盈亏
     */
    double updateProfit(const std::string& accountId,
                        const std::string& instrumentId,
                        double lastPrice,
                        int volumeMultiple);

    /**
     * @brief 获取账户总浮动盈亏
     *
     * @param accountId 账户ID
     * @return 总浮动盈亏
     */
    double getTotalProfit(const std::string& accountId) const;

    // -------------------------------------------------------------------------
    // 清理方法
    // -------------------------------------------------------------------------

    /**
     * @brief 清空所有持仓
     */
    void clear();

private:
    /**
     * @brief 生成持仓键
     *
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @return 复合键
     */
    static std::string makeKey(const std::string& accountId, const std::string& instrumentId);

    /**
     * @brief 持久化持仓（内部方法）
     *
     * @param position 要持久化的持仓
     */
    void persistPosition(const Position& position);

    /// 持仓映射表：accountId_instrumentId -> Position
    std::unordered_map<std::string, Position> positions_;

    /// 存储接口（可为nullptr）
    IStore* store_;

    /// 互斥锁，保护 positions_
    mutable std::mutex mutex_;
};

} // namespace fix40
