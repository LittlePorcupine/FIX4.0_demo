/**
 * @file client_state.cpp
 * @brief 客户端状态管理实现
 */

#include "client_state.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdlib>

namespace fix40::client {

// ============================================================================
// 连接状态
// ============================================================================

void ClientState::setConnectionState(ConnectionState state) {
    connectionState_.store(state);
    notifyStateChange();
}

ConnectionState ClientState::getConnectionState() const {
    return connectionState_.load();
}

std::string ClientState::getConnectionStateString() const {
    switch (connectionState_.load()) {
        case ConnectionState::DISCONNECTED: return "断开连接";
        case ConnectionState::CONNECTING:   return "连接中...";
        case ConnectionState::CONNECTED:    return "已连接";
        case ConnectionState::LOGGING_IN:   return "登录中...";
        case ConnectionState::LOGGED_IN:    return "已登录";
        case ConnectionState::ERROR:        return "错误";
        default: return "未知";
    }
}

void ClientState::setUserId(const std::string& userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    userId_ = userId;
}

std::string ClientState::getUserId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return userId_;
}

// ============================================================================
// 账户信息
// ============================================================================

void ClientState::updateAccount(const AccountInfo& info) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        account_ = info;
    }
    notifyStateChange();
}

AccountInfo ClientState::getAccount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_;
}

// ============================================================================
// 持仓信息
// ============================================================================

void ClientState::updatePosition(const PositionInfo& pos) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(positions_.begin(), positions_.end(),
            [&](const PositionInfo& p) { return p.instrumentId == pos.instrumentId; });
        if (it != positions_.end()) {
            // 合并更新：如果新数据有值则更新，否则保留旧值
            if (pos.longPosition > 0 || pos.shortPosition > 0) {
                it->longPosition = pos.longPosition;
                it->shortPosition = pos.shortPosition;
            }
            if (pos.longAvgPrice > 0) it->longAvgPrice = pos.longAvgPrice;
            if (pos.shortAvgPrice > 0) it->shortAvgPrice = pos.shortAvgPrice;
            // 盈亏始终更新（可能为负）
            it->profit = pos.profit;
        } else {
            positions_.push_back(pos);
        }
    }
    notifyStateChange();
}

void ClientState::setPositions(const std::vector<PositionInfo>& positions) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_ = positions;
    }
    notifyStateChange();
}

std::vector<PositionInfo> ClientState::getPositions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positions_;
}

void ClientState::clearPositions() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_.clear();
    }
    notifyStateChange();
}

// ============================================================================
// 订单信息
// ============================================================================

void ClientState::addOrder(const OrderInfo& order) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (orders_.find(order.clOrdID) == orders_.end()) {
            orderSequence_.push_back(order.clOrdID);
        }
        orders_[order.clOrdID] = order;
    }
    notifyStateChange();
}

void ClientState::updateOrder(const std::string& clOrdID, const OrderInfo& order) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (orders_.find(clOrdID) == orders_.end()) {
            orderSequence_.push_back(clOrdID);
        }
        orders_[clOrdID] = order;
    }
    notifyStateChange();
}

std::vector<OrderInfo> ClientState::getOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OrderInfo> result;
    result.reserve(orderSequence_.size());
    for (const auto& clOrdID : orderSequence_) {
        auto it = orders_.find(clOrdID);
        if (it != orders_.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

std::vector<OrderInfo> ClientState::getActiveOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OrderInfo> result;
    result.reserve(orderSequence_.size());
    for (const auto& clOrdID : orderSequence_) {
        auto it = orders_.find(clOrdID);
        if (it == orders_.end()) {
            continue;
        }
        const auto& order = it->second;
        if (order.state == OrderState::PENDING_NEW ||
            order.state == OrderState::NEW ||
            order.state == OrderState::PARTIALLY_FILLED) {
            result.push_back(order);
        }
    }
    return result;
}

void ClientState::clearOrders() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_.clear();
        orderSequence_.clear();
    }
    notifyStateChange();
}

void ClientState::setOrders(const std::vector<OrderInfo>& orders) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_.clear();
        orderSequence_.clear();
        orderSequence_.reserve(orders.size());
        for (const auto& order : orders) {
            if (order.clOrdID.empty()) {
                continue;
            }
            orderSequence_.push_back(order.clOrdID);
            orders_[order.clOrdID] = order;
        }
    }
    notifyStateChange();
}

static std::string getDefaultOrdersPath() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.fix_client_orders.dat";
    }
    return ".fix_client_orders.dat";
}

void ClientState::saveOrders(const std::string& filepath) {
    std::string path = filepath.empty() ? getDefaultOrdersPath() : filepath;
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ofstream ofs(path);
    if (!ofs) return;
    
    // 简单的文本格式：每行一个订单，字段用 | 分隔
    for (const auto& clOrdID : orderSequence_) {
        auto it = orders_.find(clOrdID);
        if (it == orders_.end()) {
            continue;
        }
        const auto& order = it->second;
        ofs << order.clOrdID << "|"
            << order.orderId << "|"
            << order.symbol << "|"
            << order.side << "|"
            << order.price << "|"
            << order.orderQty << "|"
            << order.filledQty << "|"
            << order.avgPx << "|"
            << static_cast<int>(order.state) << "|"
            << order.text << "|"
            << order.updateTime << "\n";
    }
}

void ClientState::loadOrders(const std::string& filepath) {
    std::string path = filepath.empty() ? getDefaultOrdersPath() : filepath;
    
    std::ifstream ifs(path);
    if (!ifs) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    orders_.clear();
    orderSequence_.clear();
    
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::istringstream iss(line);
        std::string field;
        while (std::getline(iss, field, '|')) {
            fields.push_back(field);
        }
        
        // 至少需要 9 个字段（到 state）
        if (fields.size() >= 9) {
            try {
                OrderInfo order;
                order.clOrdID = fields[0];
                order.orderId = fields[1];
                order.symbol = fields[2];
                order.side = fields[3];
                order.price = std::stod(fields[4]);
                order.orderQty = std::stoll(fields[5]);
                order.filledQty = std::stoll(fields[6]);
                order.avgPx = std::stod(fields[7]);
                order.state = static_cast<OrderState>(std::stoi(fields[8]));
                if (fields.size() > 9) order.text = fields[9];
                if (fields.size() > 10) order.updateTime = fields[10];
                orders_[order.clOrdID] = order;
                orderSequence_.push_back(order.clOrdID);
            } catch (...) {
                // 解析失败，跳过该条目
            }
        }
    }

    // 旧版本保存顺序来自 unordered_map，可能是“随机”的。
    // 这里按 clOrdID 做一次稳定排序，至少能让展示顺序可预测（也更接近下单序号）。
    std::sort(orderSequence_.begin(), orderSequence_.end());
    orderSequence_.erase(std::unique(orderSequence_.begin(), orderSequence_.end()),
                         orderSequence_.end());
}

// ============================================================================
// 合约搜索结果
// ============================================================================

void ClientState::setSearchResults(const std::vector<std::string>& results) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        searchResults_ = results;
    }
    notifyStateChange();
}

std::vector<std::string> ClientState::getSearchResults() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return searchResults_;
}

// ============================================================================
// 状态变更通知
// ============================================================================

void ClientState::setOnStateChange(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onStateChange_ = std::move(callback);
}

void ClientState::notifyStateChange() {
    StateChangeCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 节流：最多每 50ms 通知一次，避免高频刷新导致崩溃
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNotifyTime_);
        if (elapsed.count() < 50) {
            return;
        }
        lastNotifyTime_ = now;
        
        callback = onStateChange_;
    }
    if (callback) {
        callback();
    }
}

// ============================================================================
// 消息/错误
// ============================================================================

void ClientState::setLastError(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = error;
    }
    notifyStateChange();
}

std::string ClientState::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void ClientState::addMessage(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif
        oss << std::put_time(&tm_buf, "%H:%M:%S") << " " << msg;
        messages_.push_back(oss.str());
        // 保留最近 100 条消息
        if (messages_.size() > 100) {
            messages_.erase(messages_.begin());
        }
    }
    notifyStateChange();
}

std::vector<std::string> ClientState::getMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_;
}

} // namespace fix40::client
