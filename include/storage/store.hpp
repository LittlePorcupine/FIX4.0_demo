/**
 * @file store.hpp
 * @brief 持久化存储抽象接口
 *
 * 定义订单、成交、消息、账户、持仓等数据的存储接口。
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "app/order.hpp"
#include "app/account.hpp"
#include "app/position.hpp"

namespace fix40 {

/**
 * @brief 存储用成交记录
 */
struct StoredTrade {
    std::string tradeId;        ///< 成交编号
    std::string clOrdID;        ///< 客户订单号
    std::string symbol;         ///< 合约代码
    OrderSide side;             ///< 买卖方向
    double price;               ///< 成交价格
    int64_t quantity;           ///< 成交数量
    int64_t timestamp;          ///< 成交时间戳 (毫秒)
    std::string counterpartyOrderId; ///< 对手方订单号
};

/**
 * @brief 会话状态 (用于断线恢复)
 */
struct SessionState {
    std::string senderCompID;
    std::string targetCompID;
    int sendSeqNum;             ///< 发送序列号
    int recvSeqNum;             ///< 接收序列号
    int64_t lastUpdateTime;     ///< 最后更新时间
};

/**
 * @brief 存储的消息 (用于重传)
 */
struct StoredMessage {
    int seqNum;                 ///< 序列号
    std::string senderCompID;
    std::string targetCompID;
    std::string msgType;        ///< 消息类型
    std::string rawMessage;     ///< 原始消息
    int64_t timestamp;          ///< 时间戳
};

/**
 * @class IStore
 * @brief 存储接口
 */
class IStore {
public:
    virtual ~IStore() = default;

    // =========================================================================
    // 订单存储
    // =========================================================================
    
    virtual bool saveOrder(const Order& order) = 0;
    virtual bool updateOrder(const Order& order) = 0;
    virtual std::optional<Order> loadOrder(const std::string& clOrdID) = 0;
    virtual std::vector<Order> loadOrdersBySymbol(const std::string& symbol) = 0;
    virtual std::vector<Order> loadActiveOrders() = 0;
    virtual std::vector<Order> loadAllOrders() = 0;

    // =========================================================================
    // 成交存储
    // =========================================================================
    
    virtual bool saveTrade(const StoredTrade& trade) = 0;
    virtual std::vector<StoredTrade> loadTradesByOrder(const std::string& clOrdID) = 0;
    virtual std::vector<StoredTrade> loadTradesBySymbol(const std::string& symbol) = 0;

    // =========================================================================
    // 会话状态存储
    // =========================================================================
    
    virtual bool saveSessionState(const SessionState& state) = 0;
    virtual std::optional<SessionState> loadSessionState(
        const std::string& senderCompID, const std::string& targetCompID) = 0;

    // =========================================================================
    // 消息存储 (用于重传)
    // =========================================================================
    
    virtual bool saveMessage(const StoredMessage& msg) = 0;
    virtual std::vector<StoredMessage> loadMessages(
        const std::string& senderCompID, const std::string& targetCompID,
        int beginSeqNum, int endSeqNum) = 0;
    virtual bool deleteMessagesOlderThan(int64_t timestamp) = 0;

    // =========================================================================
    // 账户存储
    // =========================================================================
    
    /**
     * @brief 保存账户信息
     * 
     * 将账户状态持久化到数据库。如果账户已存在则更新，否则插入新记录。
     * 
     * @param account 要保存的账户对象
     * @return 保存成功返回 true，失败返回 false
     * 
     * @note 此方法使用 INSERT OR REPLACE 语义，确保幂等性
     */
    virtual bool saveAccount(const Account& account) = 0;
    
    /**
     * @brief 加载账户信息
     * 
     * 从数据库加载指定账户的状态。
     * 
     * @param accountId 账户ID
     * @return 如果账户存在返回 Account 对象，否则返回 std::nullopt
     */
    virtual std::optional<Account> loadAccount(const std::string& accountId) = 0;
    
    /**
     * @brief 加载所有账户
     * 
     * 从数据库加载所有账户信息。
     * 
     * @return 账户列表
     */
    virtual std::vector<Account> loadAllAccounts() = 0;
    
    /**
     * @brief 删除账户
     * 
     * 从数据库删除指定账户。
     * 
     * @param accountId 账户ID
     * @return 删除成功返回 true，失败返回 false
     * 
     * @warning 删除账户不会自动删除关联的持仓数据，需要单独处理
     */
    virtual bool deleteAccount(const std::string& accountId) = 0;

    // =========================================================================
    // 持仓存储
    // =========================================================================
    
    /**
     * @brief 保存持仓信息
     * 
     * 将持仓状态持久化到数据库。如果持仓已存在则更新，否则插入新记录。
     * 
     * @param position 要保存的持仓对象
     * @return 保存成功返回 true，失败返回 false
     * 
     * @note 此方法使用 INSERT OR REPLACE 语义，确保幂等性
     */
    virtual bool savePosition(const Position& position) = 0;
    
    /**
     * @brief 加载持仓信息
     * 
     * 从数据库加载指定账户在指定合约上的持仓。
     * 
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @return 如果持仓存在返回 Position 对象，否则返回 std::nullopt
     */
    virtual std::optional<Position> loadPosition(
        const std::string& accountId, const std::string& instrumentId) = 0;
    
    /**
     * @brief 加载账户的所有持仓
     * 
     * 从数据库加载指定账户的所有持仓信息。
     * 
     * @param accountId 账户ID
     * @return 持仓列表
     */
    virtual std::vector<Position> loadPositionsByAccount(const std::string& accountId) = 0;
    
    /**
     * @brief 加载所有持仓
     * 
     * 从数据库加载所有持仓信息。
     * 
     * @return 持仓列表
     */
    virtual std::vector<Position> loadAllPositions() = 0;
    
    /**
     * @brief 删除持仓
     * 
     * 从数据库删除指定持仓。
     * 
     * @param accountId 账户ID
     * @param instrumentId 合约代码
     * @return 删除成功返回 true，失败返回 false
     */
    virtual bool deletePosition(const std::string& accountId, const std::string& instrumentId) = 0;
    
    /**
     * @brief 删除账户的所有持仓
     * 
     * 从数据库删除指定账户的所有持仓。
     * 
     * @param accountId 账户ID
     * @return 删除成功返回 true，失败返回 false
     */
    virtual bool deletePositionsByAccount(const std::string& accountId) = 0;
};

} // namespace fix40
