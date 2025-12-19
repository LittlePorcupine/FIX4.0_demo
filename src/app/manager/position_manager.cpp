/**
 * @file position_manager.cpp
 * @brief 持仓管理模块实现
 */

#include "app/manager/position_manager.hpp"
#include "storage/store.hpp"
#include <chrono>

namespace fix40 {

// =============================================================================
// 构造函数
// =============================================================================

PositionManager::PositionManager()
    : store_(nullptr)
{}

PositionManager::PositionManager(IStore* store)
    : store_(store)
{
    if (store_) {
        // 启动时从存储恢复持仓状态（用于服务端重启后持仓连续性）。
        auto positions = store_->loadAllPositions();
        std::lock_guard<std::mutex> lock(mutex_);
        positions_.clear();
        for (const auto& position : positions) {
            if (!position.accountId.empty() && !position.instrumentId.empty()) {
                positions_[makeKey(position.accountId, position.instrumentId)] = position;
            }
        }
    }
}

// =============================================================================
// 查询方法
// =============================================================================

std::optional<Position> PositionManager::getPosition(const std::string& accountId,
                                                      const std::string& instrumentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = positions_.find(makeKey(accountId, instrumentId));
    if (it != positions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Position> PositionManager::getPositionsByAccount(const std::string& accountId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Position> result;
    for (const auto& pair : positions_) {
        if (pair.second.accountId == accountId) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<Position> PositionManager::getAllPositions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Position> result;
    result.reserve(positions_.size());
    for (const auto& pair : positions_) {
        result.push_back(pair.second);
    }
    return result;
}

bool PositionManager::hasPosition(const std::string& accountId,
                                   const std::string& instrumentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = positions_.find(makeKey(accountId, instrumentId));
    if (it == positions_.end()) {
        return false;
    }
    return it->second.hasPosition();
}

size_t PositionManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positions_.size();
}

// =============================================================================
// 开仓操作
// =============================================================================

void PositionManager::openPosition(const std::string& accountId,
                                    const std::string& instrumentId,
                                    OrderSide side,
                                    int64_t volume,
                                    double price,
                                    double margin) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = makeKey(accountId, instrumentId);
    
    // 获取或创建持仓
    auto it = positions_.find(key);
    if (it == positions_.end()) {
        Position pos(accountId, instrumentId);
        positions_[key] = pos;
        it = positions_.find(key);
    }
    
    Position& pos = it->second;
    
    if (side == OrderSide::BUY) {
        // 多头开仓
        // 新均价 = (原均价 × 原持仓 + 开仓价 × 开仓量) / (原持仓 + 开仓量)
        double totalCost = pos.longAvgPrice * pos.longPosition + price * volume;
        int64_t totalVolume = pos.longPosition + volume;
        
        pos.longPosition = totalVolume;
        pos.longAvgPrice = totalVolume > 0 ? totalCost / totalVolume : 0.0;
        pos.longMargin += margin;
    } else {
        // 空头开仓
        double totalCost = pos.shortAvgPrice * pos.shortPosition + price * volume;
        int64_t totalVolume = pos.shortPosition + volume;
        
        pos.shortPosition = totalVolume;
        pos.shortAvgPrice = totalVolume > 0 ? totalCost / totalVolume : 0.0;
        pos.shortMargin += margin;
    }
    
    pos.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistPosition(pos);
}

// =============================================================================
// 平仓操作
// =============================================================================

double PositionManager::closePosition(const std::string& accountId,
                                       const std::string& instrumentId,
                                       OrderSide side,
                                       int64_t volume,
                                       double price,
                                       int volumeMultiple) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = makeKey(accountId, instrumentId);
    
    auto it = positions_.find(key);
    if (it == positions_.end()) {
        return 0.0;
    }
    
    Position& pos = it->second;
    double profit = 0.0;
    
    if (side == OrderSide::SELL) {
        // 平多头（卖出平仓）
        // 盈亏 = (平仓价 - 持仓均价) × 平仓量 × 合约乘数
        profit = (price - pos.longAvgPrice) * volume * volumeMultiple;
        
        // 计算平仓前的持仓量，用于按比例计算保证金释放
        int64_t originalPosition = pos.longPosition;
        
        // 减少持仓
        pos.longPosition -= volume;
        if (pos.longPosition <= 0) {
            pos.longPosition = 0;
            pos.longAvgPrice = 0.0;
            pos.longProfit = 0.0;
            pos.longMargin = 0.0;
        } else {
            // 按比例减少保证金：释放的保证金 = 原保证金 × (平仓量 / 原持仓量)
            double marginToRelease = pos.longMargin * (static_cast<double>(volume) / originalPosition);
            pos.longMargin -= marginToRelease;
            // 浮动盈亏与持仓量成比例：若当前 longProfit 基于某个最新价计算过，平仓后按剩余仓位缩放，
            // 避免出现“持仓量减少但浮动盈亏仍保持旧值”的情况。
            pos.longProfit *= (static_cast<double>(pos.longPosition) / originalPosition);
        }
    } else {
        // 平空头（买入平仓）
        // 盈亏 = (持仓均价 - 平仓价) × 平仓量 × 合约乘数
        profit = (pos.shortAvgPrice - price) * volume * volumeMultiple;
        
        // 计算平仓前的持仓量，用于按比例计算保证金释放
        int64_t originalPosition = pos.shortPosition;
        
        // 减少持仓
        pos.shortPosition -= volume;
        if (pos.shortPosition <= 0) {
            pos.shortPosition = 0;
            pos.shortAvgPrice = 0.0;
            pos.shortProfit = 0.0;
            pos.shortMargin = 0.0;
        } else {
            // 按比例减少保证金：释放的保证金 = 原保证金 × (平仓量 / 原持仓量)
            double marginToRelease = pos.shortMargin * (static_cast<double>(volume) / originalPosition);
            pos.shortMargin -= marginToRelease;
            pos.shortProfit *= (static_cast<double>(pos.shortPosition) / originalPosition);
        }
    }
    
    pos.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistPosition(pos);
    
    return profit;
}

// =============================================================================
// 盈亏更新
// =============================================================================

void PositionManager::updateAllProfits(const MarketDataSnapshot& snapshot, int volumeMultiple) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& pair : positions_) {
        if (pair.second.instrumentId == snapshot.instrumentId) {
            pair.second.updateProfit(snapshot.lastPrice, volumeMultiple);
            pair.second.updateTime = std::chrono::system_clock::now();
        }
    }
}

double PositionManager::updateProfit(const std::string& accountId,
                                      const std::string& instrumentId,
                                      double lastPrice,
                                      int volumeMultiple) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = makeKey(accountId, instrumentId);
    
    auto it = positions_.find(key);
    if (it == positions_.end()) {
        return 0.0;
    }
    
    Position& pos = it->second;
    pos.updateProfit(lastPrice, volumeMultiple);
    pos.updateTime = std::chrono::system_clock::now();
    
    return pos.getTotalProfit();
}

double PositionManager::getTotalProfit(const std::string& accountId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double total = 0.0;
    for (const auto& pair : positions_) {
        if (pair.second.accountId == accountId) {
            total += pair.second.getTotalProfit();
        }
    }
    return total;
}

// =============================================================================
// 清理方法
// =============================================================================

void PositionManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.clear();
}

// =============================================================================
// 私有方法
// =============================================================================

std::string PositionManager::makeKey(const std::string& accountId,
                                      const std::string& instrumentId) {
    return accountId + "_" + instrumentId;
}

void PositionManager::persistPosition(const Position& position) {
    if (!store_) return;
    // 注意：此处为 best-effort 持久化。存储失败时不抛异常，以免影响撮合主流程。
    store_->savePosition(position);
}

} // namespace fix40
