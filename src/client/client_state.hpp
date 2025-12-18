/**
 * @file client_state.hpp
 * @brief 客户端状态管理
 *
 * 管理客户端的账户、持仓、订单等状态数据。
 * 线程安全，支持从 FIX 回调更新和 TUI 读取。
 */

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <chrono>

namespace fix40::client {

/**
 * @brief 连接状态
 */
enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    LOGGING_IN,
    LOGGED_IN,
    ERROR
};

/**
 * @brief 账户信息
 */
struct AccountInfo {
    double balance = 0.0;         ///< 静态权益
    double available = 0.0;       ///< 可用资金
    double frozenMargin = 0.0;    ///< 冻结保证金
    double usedMargin = 0.0;      ///< 占用保证金
    double positionProfit = 0.0;  ///< 持仓盈亏
    double closeProfit = 0.0;     ///< 平仓盈亏
    double dynamicEquity = 0.0;   ///< 动态权益
    double riskRatio = 0.0;       ///< 风险度
};

/**
 * @brief 持仓信息
 */
struct PositionInfo {
    std::string instrumentId;
    int64_t longPosition = 0;
    double longAvgPrice = 0.0;
    int64_t shortPosition = 0;
    double shortAvgPrice = 0.0;
    double profit = 0.0;
};

/**
 * @brief 订单状态
 */
enum class OrderState {
    PENDING_NEW,
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED
};

/**
 * @brief 订单信息
 */
struct OrderInfo {
    std::string clOrdID;
    std::string orderId;
    std::string symbol;
    std::string side;           ///< "BUY" or "SELL"
    double price = 0.0;
    int64_t orderQty = 0;
    int64_t filledQty = 0;
    double avgPx = 0.0;
    OrderState state = OrderState::PENDING_NEW;
    std::string text;           ///< 拒绝原因等
    std::string updateTime;
};

/**
 * @class ClientState
 * @brief 客户端状态管理器
 *
 * 线程安全地管理客户端状态，支持：
 * - 从 FIX 回调线程更新状态
 * - 从 TUI 渲染线程读取状态
 * - 状态变更通知
 */
class ClientState {
public:
    using StateChangeCallback = std::function<void()>;

    ClientState() = default;

    // =========================================================================
    // 连接状态
    // =========================================================================
    
    void setConnectionState(ConnectionState state);
    ConnectionState getConnectionState() const;
    std::string getConnectionStateString() const;
    
    void setUserId(const std::string& userId);
    std::string getUserId() const;

    // =========================================================================
    // 账户信息
    // =========================================================================
    
    void updateAccount(const AccountInfo& info);
    AccountInfo getAccount() const;

    // =========================================================================
    // 持仓信息
    // =========================================================================
    
    void updatePosition(const PositionInfo& pos);
    void setPositions(const std::vector<PositionInfo>& positions);
    std::vector<PositionInfo> getPositions() const;
    void clearPositions();

    // =========================================================================
    // 订单信息
    // =========================================================================
    
    void addOrder(const OrderInfo& order);
    void updateOrder(const std::string& clOrdID, const OrderInfo& order);
    std::vector<OrderInfo> getOrders() const;
    std::vector<OrderInfo> getActiveOrders() const;
    void clearOrders();

    /**
     * @brief 批量设置订单列表
     *
     * 用于“从服务端持久化历史刷新订单列表”等场景：
     * 一次性替换内部订单容器并只触发一次 notifyStateChange()，避免逐条 addOrder 导致的频繁刷新。
     *
     * @param orders 新的订单列表（按希望展示的顺序排列）
     */
    void setOrders(const std::vector<OrderInfo>& orders);
    
    /**
     * @brief 保存订单到文件
     * @param filepath 文件路径（默认 ~/.fix_client_orders.dat）
     */
    void saveOrders(const std::string& filepath = "");
    
    /**
     * @brief 从文件加载订单
     * @param filepath 文件路径（默认 ~/.fix_client_orders.dat）
     */
    void loadOrders(const std::string& filepath = "");

    // =========================================================================
    // 合约搜索结果
    // =========================================================================
    
    void setSearchResults(const std::vector<std::string>& results);
    std::vector<std::string> getSearchResults() const;

    // =========================================================================
    // 状态变更通知
    // =========================================================================
    
    void setOnStateChange(StateChangeCallback callback);
    void notifyStateChange();

    // =========================================================================
    // 消息/错误
    // =========================================================================
    
    void setLastError(const std::string& error);
    std::string getLastError() const;
    
    void addMessage(const std::string& msg);
    std::vector<std::string> getMessages() const;

private:
    mutable std::mutex mutex_;
    
    // 连接状态
    std::atomic<ConnectionState> connectionState_{ConnectionState::DISCONNECTED};
    std::string userId_;
    
    // 账户信息
    AccountInfo account_;
    
    // 持仓信息
    std::vector<PositionInfo> positions_;
    
    // 订单信息
    std::unordered_map<std::string, OrderInfo> orders_;
    // 订单插入顺序（用于稳定展示顺序/保存顺序）
    std::vector<std::string> orderSequence_;
    
    // 合约搜索结果
    std::vector<std::string> searchResults_;
    
    // 消息
    std::vector<std::string> messages_;
    std::string lastError_;
    
    // 状态变更回调
    StateChangeCallback onStateChange_;
    
    // 节流：上次通知时间
    mutable std::chrono::steady_clock::time_point lastNotifyTime_;
};

} // namespace fix40::client
