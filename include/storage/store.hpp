/**
 * @file store.hpp
 * @brief 持久化存储抽象接口
 *
 * 定义订单、成交、消息等数据的存储接口。
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "app/order.hpp"

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
};

} // namespace fix40
