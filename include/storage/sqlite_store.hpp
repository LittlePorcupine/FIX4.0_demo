/**
 * @file sqlite_store.hpp
 * @brief SQLite 持久化存储实现
 */

#pragma once

#include "storage/store.hpp"
#include <sqlite3.h>
#include <memory>
#include <mutex>

namespace fix40 {

/**
 * @class SqliteStore
 * @brief SQLite 存储实现
 *
 * 线程安全的 SQLite 存储实现。
 * 支持内存数据库 (":memory:") 用于测试。
 */
class SqliteStore : public IStore {
public:
    /**
     * @brief 构造函数
     * @param dbPath 数据库文件路径，":memory:" 表示内存数据库
     */
    explicit SqliteStore(const std::string& dbPath);
    
    ~SqliteStore() override;

    // 禁止拷贝
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    /**
     * @brief 检查数据库是否已打开
     */
    bool isOpen() const { return db_ != nullptr; }

    // IStore 接口实现
    bool saveOrder(const Order& order) override;
    bool saveOrderForAccount(const Order& order, const std::string& accountId) override;
    bool updateOrder(const Order& order) override;
    std::optional<Order> loadOrder(const std::string& clOrdID) override;
    std::vector<Order> loadOrdersBySymbol(const std::string& symbol) override;
    std::vector<Order> loadOrdersByAccount(const std::string& accountId) override;
    std::vector<Order> loadActiveOrders() override;
    std::vector<Order> loadAllOrders() override;

    bool saveTrade(const StoredTrade& trade) override;
    std::vector<StoredTrade> loadTradesByOrder(const std::string& clOrdID) override;
    std::vector<StoredTrade> loadTradesBySymbol(const std::string& symbol) override;

    bool saveSessionState(const SessionState& state) override;
    std::optional<SessionState> loadSessionState(
        const std::string& senderCompID, const std::string& targetCompID) override;

    bool saveMessage(const StoredMessage& msg) override;
    std::vector<StoredMessage> loadMessages(
        const std::string& senderCompID, const std::string& targetCompID,
        int beginSeqNum, int endSeqNum) override;
    bool deleteMessagesForSession(
        const std::string& senderCompID, const std::string& targetCompID) override;
    bool deleteMessagesOlderThan(int64_t timestamp) override;

    // 账户存储
    bool saveAccount(const Account& account) override;
    std::optional<Account> loadAccount(const std::string& accountId) override;
    std::vector<Account> loadAllAccounts() override;
    bool deleteAccount(const std::string& accountId) override;

    // 持仓存储
    bool savePosition(const Position& position) override;
    std::optional<Position> loadPosition(
        const std::string& accountId, const std::string& instrumentId) override;
    std::vector<Position> loadPositionsByAccount(const std::string& accountId) override;
    std::vector<Position> loadAllPositions() override;
    bool deletePosition(const std::string& accountId, const std::string& instrumentId) override;
    bool deletePositionsByAccount(const std::string& accountId) override;

private:
    /**
     * @brief 初始化数据库表
     */
    bool initTables();

    /**
     * @brief 执行 SQL 语句
     */
    bool execute(const std::string& sql);

    /**
     * @brief 从 SQLite 结果行提取 Order 对象
     */
    Order extractOrder(sqlite3_stmt* stmt);

    /**
     * @brief 从 SQLite 结果行提取 StoredTrade 对象
     */
    StoredTrade extractTrade(sqlite3_stmt* stmt);

    /**
     * @brief 从 SQLite 结果行提取 Account 对象
     */
    Account extractAccount(sqlite3_stmt* stmt);

    /**
     * @brief 从 SQLite 结果行提取 Position 对象
     */
    Position extractPosition(sqlite3_stmt* stmt);

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fix40
