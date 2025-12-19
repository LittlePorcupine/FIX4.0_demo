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
    // 启用外键约束
    execute("PRAGMA foreign_keys=ON");

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
            account_id TEXT NOT NULL DEFAULT '',
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

    // 账户表
    const char* createAccounts = R"(
        CREATE TABLE IF NOT EXISTS accounts (
            account_id TEXT PRIMARY KEY,
            balance REAL NOT NULL DEFAULT 0,
            available REAL NOT NULL DEFAULT 0,
            frozen_margin REAL NOT NULL DEFAULT 0,
            used_margin REAL NOT NULL DEFAULT 0,
            position_profit REAL NOT NULL DEFAULT 0,
            close_profit REAL NOT NULL DEFAULT 0,
            update_time INTEGER NOT NULL
        )
    )";

    // 持仓表
    const char* createPositions = R"(
        CREATE TABLE IF NOT EXISTS positions (
            account_id TEXT NOT NULL,
            instrument_id TEXT NOT NULL,
            long_position INTEGER NOT NULL DEFAULT 0,
            long_avg_price REAL NOT NULL DEFAULT 0,
            long_profit REAL NOT NULL DEFAULT 0,
            long_margin REAL NOT NULL DEFAULT 0,
            short_position INTEGER NOT NULL DEFAULT 0,
            short_avg_price REAL NOT NULL DEFAULT 0,
            short_profit REAL NOT NULL DEFAULT 0,
            short_margin REAL NOT NULL DEFAULT 0,
            update_time INTEGER NOT NULL,
            PRIMARY KEY (account_id, instrument_id)
        )
    )";

    // 创建索引
    const char* createIndexes = R"(
        CREATE INDEX IF NOT EXISTS idx_orders_symbol ON orders(symbol);
        CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
        CREATE INDEX IF NOT EXISTS idx_orders_account ON orders(account_id);
        CREATE INDEX IF NOT EXISTS idx_trades_cl_ord_id ON trades(cl_ord_id);
        CREATE INDEX IF NOT EXISTS idx_trades_symbol ON trades(symbol);
        CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(sender_comp_id, target_comp_id, seq_num);
        CREATE INDEX IF NOT EXISTS idx_positions_account ON positions(account_id);
    )";

    // 兼容旧数据库：为 orders 表补齐 account_id 字段（用于按用户隔离查询订单历史）。
    // SQLite 不支持 IF NOT EXISTS，因此这里容错“重复列名”错误。
    auto addOrderAccountIdColumn = [&]() -> bool {
        if (!db_) return false;
        char* errMsg = nullptr;
        const char* sql = "ALTER TABLE orders ADD COLUMN account_id TEXT NOT NULL DEFAULT ''";
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
        if (rc == SQLITE_OK) {
            return true;
        }
        const std::string err = errMsg ? std::string(errMsg) : "";
        sqlite3_free(errMsg);
        if (err.find("duplicate column name") != std::string::npos) {
            return true;
        }
        LOG() << "[SqliteStore] SQL 执行失败: " << err;
        return false;
    };

    return execute(createOrders) && addOrderAccountIdColumn() && execute(createTrades) && 
           execute(createSessions) && execute(createMessages) &&
           execute(createAccounts) && execute(createPositions) && execute(createIndexes);
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
    const char* clOrdID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* orderID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    order.clOrdID = clOrdID ? clOrdID : "";
    order.orderID = orderID ? orderID : "";
    order.symbol = symbol ? symbol : "";
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
    return saveOrderForAccount(order, "");
}

bool SqliteStore::saveOrderForAccount(const Order& order, const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT INTO orders (cl_ord_id, order_id, account_id, symbol, side, order_type, time_in_force,
                           price, order_qty, cum_qty, leaves_qty, avg_px, status, create_time, update_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
    sqlite3_bind_text(stmt, 3, accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, order.symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, static_cast<int>(order.side));
    sqlite3_bind_int(stmt, 6, static_cast<int>(order.ordType));
    sqlite3_bind_int(stmt, 7, static_cast<int>(order.timeInForce));
    sqlite3_bind_double(stmt, 8, order.price);
    sqlite3_bind_int64(stmt, 9, order.orderQty);
    sqlite3_bind_int64(stmt, 10, order.cumQty);
    sqlite3_bind_int64(stmt, 11, order.leavesQty);
    sqlite3_bind_double(stmt, 12, order.avgPx);
    sqlite3_bind_int(stmt, 13, static_cast<int>(order.status));
    sqlite3_bind_int64(stmt, 14, now);
    sqlite3_bind_int64(stmt, 15, now);

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
        UPDATE orders SET
            -- 仅在新值非空时更新 order_id：
            -- 该行为使 updateOrder() 幂等且避免“错误清空”订单号；
            -- 如需显式清空字段，请提供单独的维护接口（当前暂不支持）。
            order_id = COALESCE(NULLIF(?, ''), order_id),
            cum_qty = ?,
            leaves_qty = ?,
            avg_px = ?,
            status = ?,
            update_time = ?
        WHERE cl_ord_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, order.orderID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, order.cumQty);
    sqlite3_bind_int64(stmt, 3, order.leavesQty);
    sqlite3_bind_double(stmt, 4, order.avgPx);
    sqlite3_bind_int(stmt, 5, static_cast<int>(order.status));
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_bind_text(stmt, 7, order.clOrdID.c_str(), -1, SQLITE_TRANSIENT);

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
        return orders;
    }

    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders.push_back(extractOrder(stmt));
    }

    sqlite3_finalize(stmt);
    return orders;
}

std::vector<Order> SqliteStore::loadOrdersByAccount(const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> orders;
    if (!db_) return orders;

    const char* sql = R"(
        SELECT cl_ord_id, order_id, symbol, side, order_type, time_in_force,
               price, order_qty, cum_qty, leaves_qty, avg_px, status
        FROM orders WHERE account_id = ? ORDER BY create_time DESC
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return orders;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

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
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetCompID.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionState state;
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* target = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        state.senderCompID = sender ? sender : "";
        state.targetCompID = target ? target : "";
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
        return messages;
    }

    sqlite3_bind_text(stmt, 1, senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, beginSeqNum);
    sqlite3_bind_int(stmt, 4, endSeqNum);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StoredMessage msg;
        msg.seqNum = sqlite3_column_int(stmt, 0);
        const char* sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* target = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* msgType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* rawMsg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        msg.senderCompID = sender ? sender : "";
        msg.targetCompID = target ? target : "";
        msg.msgType = msgType ? msgType : "";
        msg.rawMessage = rawMsg ? rawMsg : "";
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
        return false;
    }

    sqlite3_bind_int64(stmt, 1, timestamp);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool SqliteStore::deleteMessagesForSession(const std::string& senderCompID, const std::string& targetCompID) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "DELETE FROM messages WHERE sender_comp_id = ? AND target_comp_id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, senderCompID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, targetCompID.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

// =============================================================================
// 辅助函数：提取 Account 和 Position
// =============================================================================

Account SqliteStore::extractAccount(sqlite3_stmt* stmt) {
    Account account;
    const char* accountId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    account.accountId = accountId ? accountId : "";
    account.balance = sqlite3_column_double(stmt, 1);
    account.available = sqlite3_column_double(stmt, 2);
    account.frozenMargin = sqlite3_column_double(stmt, 3);
    account.usedMargin = sqlite3_column_double(stmt, 4);
    account.positionProfit = sqlite3_column_double(stmt, 5);
    account.closeProfit = sqlite3_column_double(stmt, 6);
    int64_t updateTimeMs = sqlite3_column_int64(stmt, 7);
    account.updateTime = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(updateTimeMs));
    return account;
}

Position SqliteStore::extractPosition(sqlite3_stmt* stmt) {
    Position position;
    const char* accountId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* instrumentId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    position.accountId = accountId ? accountId : "";
    position.instrumentId = instrumentId ? instrumentId : "";
    position.longPosition = sqlite3_column_int64(stmt, 2);
    position.longAvgPrice = sqlite3_column_double(stmt, 3);
    position.longProfit = sqlite3_column_double(stmt, 4);
    position.longMargin = sqlite3_column_double(stmt, 5);
    position.shortPosition = sqlite3_column_int64(stmt, 6);
    position.shortAvgPrice = sqlite3_column_double(stmt, 7);
    position.shortProfit = sqlite3_column_double(stmt, 8);
    position.shortMargin = sqlite3_column_double(stmt, 9);
    int64_t updateTimeMs = sqlite3_column_int64(stmt, 10);
    position.updateTime = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(updateTimeMs));
    return position;
}

// =============================================================================
// 账户存储
// =============================================================================

bool SqliteStore::saveAccount(const Account& account) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT OR REPLACE INTO accounts 
        (account_id, balance, available, frozen_margin, used_margin, 
         position_profit, close_profit, update_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG() << "[SqliteStore] 准备账户保存语句失败: " << sqlite3_errmsg(db_);
        return false;
    }

    auto updateTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        account.updateTime.time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, account.accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, account.balance);
    sqlite3_bind_double(stmt, 3, account.available);
    sqlite3_bind_double(stmt, 4, account.frozenMargin);
    sqlite3_bind_double(stmt, 5, account.usedMargin);
    sqlite3_bind_double(stmt, 6, account.positionProfit);
    sqlite3_bind_double(stmt, 7, account.closeProfit);
    sqlite3_bind_int64(stmt, 8, updateTimeMs);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG() << "[SqliteStore] 保存账户失败: " << sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::optional<Account> SqliteStore::loadAccount(const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;
    
    const char* sql = R"(
        SELECT account_id, balance, available, frozen_margin, used_margin,
               position_profit, close_profit, update_time
        FROM accounts WHERE account_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Account account = extractAccount(stmt);
        sqlite3_finalize(stmt);
        return account;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<Account> SqliteStore::loadAllAccounts() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Account> accounts;
    if (!db_) return accounts;
    
    const char* sql = R"(
        SELECT account_id, balance, available, frozen_margin, used_margin,
               position_profit, close_profit, update_time
        FROM accounts ORDER BY account_id
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return accounts;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        accounts.push_back(extractAccount(stmt));
    }

    sqlite3_finalize(stmt);
    return accounts;
}

bool SqliteStore::deleteAccount(const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = "DELETE FROM accounts WHERE account_id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

// =============================================================================
// 持仓存储
// =============================================================================

bool SqliteStore::savePosition(const Position& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = R"(
        INSERT OR REPLACE INTO positions 
        (account_id, instrument_id, long_position, long_avg_price, long_profit, long_margin,
         short_position, short_avg_price, short_profit, short_margin, update_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG() << "[SqliteStore] 准备持仓保存语句失败: " << sqlite3_errmsg(db_);
        return false;
    }

    auto updateTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        position.updateTime.time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, position.accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, position.instrumentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, position.longPosition);
    sqlite3_bind_double(stmt, 4, position.longAvgPrice);
    sqlite3_bind_double(stmt, 5, position.longProfit);
    sqlite3_bind_double(stmt, 6, position.longMargin);
    sqlite3_bind_int64(stmt, 7, position.shortPosition);
    sqlite3_bind_double(stmt, 8, position.shortAvgPrice);
    sqlite3_bind_double(stmt, 9, position.shortProfit);
    sqlite3_bind_double(stmt, 10, position.shortMargin);
    sqlite3_bind_int64(stmt, 11, updateTimeMs);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG() << "[SqliteStore] 保存持仓失败: " << sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::optional<Position> SqliteStore::loadPosition(
    const std::string& accountId, const std::string& instrumentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;
    
    const char* sql = R"(
        SELECT account_id, instrument_id, long_position, long_avg_price, long_profit, long_margin,
               short_position, short_avg_price, short_profit, short_margin, update_time
        FROM positions WHERE account_id = ? AND instrument_id = ?
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, instrumentId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Position position = extractPosition(stmt);
        sqlite3_finalize(stmt);
        return position;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<Position> SqliteStore::loadPositionsByAccount(const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Position> positions;
    if (!db_) return positions;
    
    const char* sql = R"(
        SELECT account_id, instrument_id, long_position, long_avg_price, long_profit, long_margin,
               short_position, short_avg_price, short_profit, short_margin, update_time
        FROM positions WHERE account_id = ? ORDER BY instrument_id
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return positions;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        positions.push_back(extractPosition(stmt));
    }

    sqlite3_finalize(stmt);
    return positions;
}

std::vector<Position> SqliteStore::loadAllPositions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Position> positions;
    if (!db_) return positions;
    
    const char* sql = R"(
        SELECT account_id, instrument_id, long_position, long_avg_price, long_profit, long_margin,
               short_position, short_avg_price, short_profit, short_margin, update_time
        FROM positions ORDER BY account_id, instrument_id
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return positions;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        positions.push_back(extractPosition(stmt));
    }

    sqlite3_finalize(stmt);
    return positions;
}

bool SqliteStore::deletePosition(const std::string& accountId, const std::string& instrumentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = "DELETE FROM positions WHERE account_id = ? AND instrument_id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, instrumentId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool SqliteStore::deletePositionsByAccount(const std::string& accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    
    const char* sql = "DELETE FROM positions WHERE account_id = ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, accountId.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

} // namespace fix40
