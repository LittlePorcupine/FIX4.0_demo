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
    bool updateOrder(const Order& order) override;
    std::optional<Order> loadOrder(const std::string& clOrdID) override;
    std::vector<Order> loadOrdersBySymbol(const std::string& symbol) override;
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
    bool deleteMessagesOlderThan(int64_t timestamp) override;

private:
    /**
     * @brief 初始化数据库表
     */
    bool initTables();

    /**
     * @brief 执行 SQL 语句
     */
    bool execute(const std::string& sql);

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace fix40
