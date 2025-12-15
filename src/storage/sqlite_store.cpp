/**
 * @file sqlite_store.cpp
 * @brief SQLite 持久化存储实现
 */

#include "storage/sqlite_store.hpp"
#include "base/logger.hpp"
#include <chrono>
#include <filesystem>

namespace fix40 {

SqliteStore::SqliteStore(const std::string& dbPath) {
    // 如果不是内存数据库，确保目录存在
    if (dbPath != ":memory:") {
        std::filesystem::path path(dbPath);
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                LOG() << "[SqliteStore] 创建目录失败: " << ec.message();
            }
        }
    }

    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG() << "[SqliteStore] 打开数据库失败: " << sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }

    // 启用 WAL 模式提高并发性能
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA synchronous=NORMAL");

    if (!initTables()) {
        LOG() << "[SqliteStore] 初始化表失败";
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }

    LOG() << "[SqliteStore] 数据库已打开: " << dbPath;
}

SqliteStore::~SqliteStore() {
    if (db_) {
        sqlite3_close(db_);
        LOG() << "[SqliteStore] 数据库已关闭";
    }
}

bool SqliteStore::initTables() {
    // 订单表
    const char* createOrders = R"(
        CREATE TABLE IF NOT EXISTS orders (
            cl_ord_id TEXT PRIMARY KEY,
            order_id TEXT,
            symbol TEXT NOT NULL,
            side INTEGER NOT NULL,
            order_type INTEGER NOT NULL,
            time_in_force INTEGER NOT NULL,
            price REAL NOT NULL,
            order_qty INTEGER NOT NULL,
            cum_qty INTEGER NOT NULL DEFAULT 0,
            leaves_qty INTEGER NOT NULL DEFAULT 0,
            avg_px REAL NOT NULL DEFAULT 0,
            status INTEGER NOT NULL,
            create_time INTEGER NOT NULL,
            update_time INTEGER NOT NULL
        )
    )";

    // 成交表
    const char* createTrades = R"(
        CREATE TABLE IF NOT EXISTS trades (
            trade_id TEXT PRIMARY KEY,
            cl_ord_id TEXT NOT NULL,
            symbol TEXT NOT NULL,
            side INTEGER NOT NULL,
            price REAL NOT NULL,
            quantity INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            counterparty_order_id TEXT,
            FOREIGN KEY (cl_ord_id) REFERENCES orders(cl_ord_id)
        )
    )";

    // 会话状态表
    const char* createSessions = R"(
        CREATE TABLE IF NOT EXISTS session_states (
            sender_comp_id TEXT NOT NULL,
            target_comp_id TEXT NOT NULL,
            send_seq_num INTEGER NOT NULL,
            recv_seq_num INTEGER NOT NULL,
            last_update_time INTEGER NOT NULL,
            PRIMARY KEY (sender_comp_id, target_comp_id)
        )
    )";

    // 消息存储表 (用于重传)
    const char* createMessages = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            seq_num INTEGER NOT NULL,
            sender_comp_id TEXT NOT NULL,
            target_comp_id TEXT NOT NULL,
            msg_type TEXT NOT NULL,
            raw_message TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )";

    // 创建索引
    const char* createIndexes = R"(
        CREATE INDEX IF NOT EXISTS idx_orders_symbol ON orders(symbol);
        CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
        CREATE INDEX IF NOT EXISTS idx_trades_cl_ord_id ON trades(cl_ord_id);
        CREATE INDEX IF NOT EXISTS idx_trades_symbol ON trades(symbol);
        CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(sender_comp_id, target_comp_id, seq_num);
    )";

    return execute(createOrders) && execute(createTrades) && 
           execute(createSessions) && execute(createMessages) && execute(createIndexes);
}

bool SqliteStore::execute(const std::string& sql) {
    if (!db_) {
        LOG() << "[SqliteStore] SQL 执行失败: 数据库未打开";
        return false;
    }
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        LOG() << "[SqliteStore] SQL 执行失败: " << errMsg;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// =============================================================================
// 辅助函数：从 SQLite 结果行提取对象
// =============================================================================

Order SqliteStore::extractOrder(sqlite3_stmt* stmt) {
    Order order;
    order.clOrdID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* orderID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    order.orderID = orderID ? orderID : "";
    order.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    order.side = static_cast<OrderSide>(sqlite3_column_int(stmt, 3));
    order.ordType = static_cast<OrderType>(sqlite3_column_int(stmt, 4));
    order.timeInForce = static_cast<TimeInForce>(sqlite3_column_int(stmt, 5));
    order.price = sqlite3_column_double(stmt, 6);
    order.orderQty = sqlite3_column_int64(stmt, 7);
    order.cumQty = sqlite3_column_int64(stmt, 8);
    order.leavesQty = sqlite3_column_int64(stmt, 9);
    order.avgPx = sqlite3_column_double(stmt, 10);
    order.status = static_cast<OrderStatus>(sqlite3_column_int(stmt, 11));
    return order;
}

StoredTrade SqliteStore::extractTrade(sqlite3_stmt* stmt) {
    StoredTrade trade;
    const char* tradeId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* clOrdID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    trade.tradeId = tradeId ? tradeId : "";
    trade.clOrdID = clOrdID ? clOrdID : "";
    trade.symbol = symbol ? symbol : "";
    trade.side = static_cast<OrderSide>(sqlite3_column_int(stmt, 3));
    trade.price = sqlite3_column_double(stmt, 4);
    trade.quantity = sqlite3_column_int64(stmt, 5);
    trade.timestamp = sqlite3_column_int64(stmt, 6);
    const char* cp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    trade.counterpartyOrderId = cp ? cp : "";
    return trade;
}

// =============================================================================
// 订单存储
// =============================================================================

bool SqliteStore::saveOrder(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO orders (cl_ord_id, order_id, symbol, side, order_type, time_in_force,
                           price, order_qty, cum_qty, leaves_qty, avg_px, status, create_time, update_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG() << "[SqliteStore] 准备语句失败: " << sqlite3_errmsg(db_);
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, order.clOrdID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, order.orderID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, order.symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(order.side));
    sqlite3_bind_int(stmt, 5, static_cast<int>(order.ordType));
    sqlite3_bind_int(stmt, 6, static_cast<int>(order.timeInForce));
    sqlite3_bind_double(stmt, 7, order.price);
    sqlite3_bind_int64(stmt, 8, order.orderQty);
    sqlite3_bind_int64(stmt, 9, order.cumQty);
    sqlite3_bind_int64(stmt, 10, order.leavesQty);
    sqlite3_bind_double(stmt, 11, order.avgPx);
    sqlite3_bind_int(stmt, 12, static_cast<int>(order.status));
    sqlite3_bind_int64(stmt, 13, now);
    sqlite3_bind_int64(stmt, 14, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG() << "[SqliteStore] 保存订单失败: " << sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SqliteStore::updateOrder(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        UPDATE orders SET cum_qty = ?, leaves_qty = ?, avg_px = ?, status = ?, update_time = ?
        WHERE cl_ord_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_int64(stmt, 1, order.cumQty);
    sqlite3_bind_int64(stmt, 2, order.leavesQty);
    sqlite3_bind_double(stmt, 3, order.avgPx);
    sqlite3_bind_int(stmt, 4, static_cast<int>(order.status));
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_text(stmt, 6, order.clOrdID.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::optional<Order> SqliteStore::loadOrder(const std::string& clOrdID) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;
    
    const char* sql = R"(
        SELECT cl_ord_id, order_id, symbol, side, order_type, time_in_force,
               price, order_qty, cum_qty, leaves_qty, avg_px, status
        FROM orders WHERE cl_ord_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, clOrdID.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order = extractOrder(stmt);
        sqlite3_finalize(stmt);
        return order;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<Order> SqliteStore::loadOrdersBySymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> orders;
    if (!db_) return orders;
    
    const char* sql = R"(
        SELECT cl_ord_id, order_id, symbol, side, order_type, time_in_force,
               price, order_qty, cum_qty, leaves_qty, avg_px, status
        FROM orders WHERE symbol = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return orders;
    }

    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders.push_back(extractOrder(stmt));
    }

    sqlite3_finalize(stmt);
    return orders;
}

std::vector<Order> SqliteStore::loadActiveOrders() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> orders;
    if (!db_) return orders;
    
    // 使用枚举值构建 SQL，避免硬编码魔术数字
    std::string sql = R"(
        SELECT cl_ord_id, order_id, symbol, side, order_type, time_in_force,
               price, order_qty, cum_qty, leaves_qty, avg_px, status
        FROM orders WHERE status IN ()" +
        std::to_string(static_cast<int>(OrderStatus::NEW)) + ", " +
        std::to_string(static_cast<int>(OrderStatus::PARTIALLY_FILLED)) + ", " +
        std::to_string(static_cast<int>(OrderStatus::PENDING_NEW)) + ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return orders;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders.push_back(extractOrder(stmt));
    }

    sqlite3_finalize(stmt);
    return orders;
}

std::vector<Order> SqliteStore::loadAllOrders() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> orders;
    if (!db_) return orders;
    
    const char* sql = R"(
        SELECT cl_ord_id, order_id, symbol, side, order_type, time_in_force,
               price, order_qty, cum_qty, leaves_qty, avg_px, status
        FROM orders ORDER BY create_time DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return orders;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders.push_back(extractOrder(stmt));
    }

    sqlite3_finalize(stmt);
    return orders;
}

// =============================================================================
// 成交存储
// =============================================================================

bool SqliteStore::saveTrade(const StoredTrade& trade) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO trades (trade_id, cl_ord_id, symbol, side, price, quantity,
                           timestamp, counterparty_order_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_text(stmt, 1, trade.tradeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trade.clOrdID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, trade.symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(trade.side));
    sqlite3_bind_double(stmt, 5, trade.price);
    sqlite3_bind_int64(stmt, 6, trade.quantity);
    sqlite3_bind_int64(stmt, 7, trade.timestamp);
    sqlite3_bind_text(stmt, 8, trade.counterpartyOrderId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<StoredTrade> SqliteStore::loadTradesByOrder(const std::string& clOrdID) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredTrade> trades;
    if (!db_) return trades;
    
    const char* sql = R"(
        SELECT trade_id, cl_ord_id, symbol, side, price, quantity,
               timestamp, counterparty_order_id
        FROM trades WHERE cl_ord_id = ? ORDER BY timestamp
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return trades;
    }

    sqlite3_bind_text(stmt, 1, clOrdID.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        trades.push_back(extractTrade(stmt));
    }

    sqlite3_finalize(stmt);
    return trades;
}

std::vector<StoredTrade> SqliteStore::loadTradesBySymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredTrade> trades;
    if (!db_) return trades;
    
    const char* sql = R"(
        SELECT trade_id, cl_ord_id, symbol, side, price, quantity,
               timestamp, counterparty_order_id
        FROM trades WHERE symbol = ? ORDER BY timestamp
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return trades;
    }

    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        trades.push_back(extractTrade(stmt));
    }

    sqlite3_finalize(stmt);
    return trades;
}

// =============================================================================
// 会话状态存储
// =============================================================================

bool SqliteStore::saveSessionState(const SessionState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT OR REPLACE INTO session_states 
        (sender_comp_id, target_comp_id, send_seq_num, recv_seq_num, last_update_time)
        VALUES (?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_text(stmt, 1, state.senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, state.targetCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, state.sendSeqNum);
    sqlite3_bind_int(stmt, 4, state.recvSeqNum);
    sqlite3_bind_int64(stmt, 5, state.lastUpdateTime);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::optional<SessionState> SqliteStore::loadSessionState(
    const std::string& senderCompID, const std::string& targetCompID) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;
    
    const char* sql = R"(
        SELECT sender_comp_id, target_comp_id, send_seq_num, recv_seq_num, last_update_time
        FROM session_states WHERE sender_comp_id = ? AND target_comp_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetCompID.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionState state;
        state.senderCompID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        state.targetCompID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        state.sendSeqNum = sqlite3_column_int(stmt, 2);
        state.recvSeqNum = sqlite3_column_int(stmt, 3);
        state.lastUpdateTime = sqlite3_column_int64(stmt, 4);
        sqlite3_finalize(stmt);
        return state;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

// =============================================================================
// 消息存储
// =============================================================================

bool SqliteStore::saveMessage(const StoredMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO messages (seq_num, sender_comp_id, target_comp_id, msg_type, raw_message, timestamp)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_int(stmt, 1, msg.seqNum);
    sqlite3_bind_text(stmt, 2, msg.senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, msg.targetCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.msgType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, msg.rawMessage.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, msg.timestamp);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<StoredMessage> SqliteStore::loadMessages(
    const std::string& senderCompID, const std::string& targetCompID,
    int beginSeqNum, int endSeqNum) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StoredMessage> messages;
    if (!db_) return messages;
    
    const char* sql = R"(
        SELECT seq_num, sender_comp_id, target_comp_id, msg_type, raw_message, timestamp
        FROM messages 
        WHERE sender_comp_id = ? AND target_comp_id = ? AND seq_num >= ? AND seq_num <= ?
        ORDER BY seq_num
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return messages;
    }

    sqlite3_bind_text(stmt, 1, senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, beginSeqNum);
    sqlite3_bind_int(stmt, 4, endSeqNum);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StoredMessage msg;
        msg.seqNum = sqlite3_column_int(stmt, 0);
        msg.senderCompID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.targetCompID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        msg.msgType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        msg.rawMessage = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        msg.timestamp = sqlite3_column_int64(stmt, 5);
        messages.push_back(msg);
    }

    sqlite3_finalize(stmt);
    return messages;
}

bool SqliteStore::deleteMessagesOlderThan(int64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = "DELETE FROM messages WHERE timestamp < ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, timestamp);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

} // namespace fix40
