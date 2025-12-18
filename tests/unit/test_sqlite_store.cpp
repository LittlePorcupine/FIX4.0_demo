#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "storage/sqlite_store.hpp"
#include "app/model/account.hpp"
#include "app/model/position.hpp"
#include <filesystem>
#include <cmath>

using namespace fix40;

// =============================================================================
// RapidCheck 生成器
// =============================================================================

namespace rc {

/**
 * @brief Account 生成器（用于存储测试）
 * 
 * 生成有效的 Account 对象，确保字段值在合理范围内。
 */
template<>
struct Arbitrary<Account> {
    static Gen<Account> arbitrary() {
        return gen::build<Account>(
            gen::set(&Account::accountId, gen::nonEmpty<std::string>()),
            gen::set(&Account::balance, gen::map(gen::inRange(0, 100000000), [](int v) { return v / 100.0; })),
            gen::set(&Account::available, gen::map(gen::inRange(0, 100000000), [](int v) { return v / 100.0; })),
            gen::set(&Account::frozenMargin, gen::map(gen::inRange(0, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Account::usedMargin, gen::map(gen::inRange(0, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Account::positionProfit, gen::map(gen::inRange(-10000000, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Account::closeProfit, gen::map(gen::inRange(-10000000, 10000000), [](int v) { return v / 100.0; }))
        );
    }
};

/**
 * @brief Position 生成器（用于存储测试）
 * 
 * 生成有效的 Position 对象，确保字段值在合理范围内。
 */
template<>
struct Arbitrary<Position> {
    static Gen<Position> arbitrary() {
        return gen::build<Position>(
            gen::set(&Position::accountId, gen::nonEmpty<std::string>()),
            gen::set(&Position::instrumentId, gen::nonEmpty<std::string>()),
            gen::set(&Position::longPosition, gen::inRange<int64_t>(0, 10000)),
            gen::set(&Position::longAvgPrice, gen::map(gen::inRange(1, 1000000), [](int v) { return v / 10.0; })),
            gen::set(&Position::longProfit, gen::map(gen::inRange(-10000000, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Position::longMargin, gen::map(gen::inRange(0, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Position::shortPosition, gen::inRange<int64_t>(0, 10000)),
            gen::set(&Position::shortAvgPrice, gen::map(gen::inRange(1, 1000000), [](int v) { return v / 10.0; })),
            gen::set(&Position::shortProfit, gen::map(gen::inRange(-10000000, 10000000), [](int v) { return v / 100.0; })),
            gen::set(&Position::shortMargin, gen::map(gen::inRange(0, 10000000), [](int v) { return v / 100.0; }))
        );
    }
};

} // namespace rc

TEST_CASE("SqliteStore - 基本功能", "[storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SECTION("订单存储和加载") {
        Order order;
        order.clOrdID = "ORD001";
        order.orderID = "SRV001";
        order.symbol = "IF2601";
        order.side = OrderSide::BUY;
        order.ordType = OrderType::LIMIT;
        order.timeInForce = TimeInForce::DAY;
        order.price = 4500.0;
        order.orderQty = 10;
        order.cumQty = 0;
        order.leavesQty = 10;
        order.avgPx = 0.0;
        order.status = OrderStatus::NEW;

        REQUIRE(store.saveOrder(order));

        auto loaded = store.loadOrder("ORD001");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->clOrdID == "ORD001");
        REQUIRE(loaded->symbol == "IF2601");
        REQUIRE(loaded->side == OrderSide::BUY);
        REQUIRE(loaded->price == 4500.0);
        REQUIRE(loaded->orderQty == 10);
    }

    SECTION("订单更新") {
        Order order;
        order.clOrdID = "ORD002";
        order.orderID = "";
        order.symbol = "IF2601";
        order.side = OrderSide::SELL;
        order.ordType = OrderType::LIMIT;
        order.timeInForce = TimeInForce::DAY;
        order.price = 4510.0;
        order.orderQty = 5;
        order.cumQty = 0;
        order.leavesQty = 5;
        order.status = OrderStatus::NEW;

        REQUIRE(store.saveOrder(order));

        // 模拟撮合引擎在后续回报中补充 orderID
        order.orderID = "SRV002";
        order.cumQty = 3;
        order.leavesQty = 2;
        order.avgPx = 4510.0;
        order.status = OrderStatus::PARTIALLY_FILLED;
        REQUIRE(store.updateOrder(order));

        auto loaded = store.loadOrder("ORD002");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->orderID == "SRV002");
        REQUIRE(loaded->cumQty == 3);
        REQUIRE(loaded->leavesQty == 2);
        REQUIRE(loaded->status == OrderStatus::PARTIALLY_FILLED);
    }

    SECTION("按合约加载订单") {
        Order order1, order2, order3;
        order1.clOrdID = "ORD_A1";
        order1.symbol = "IF2601";
        order1.side = OrderSide::BUY;
        order1.ordType = OrderType::LIMIT;
        order1.timeInForce = TimeInForce::DAY;
        order1.price = 4500.0;
        order1.orderQty = 10;
        order1.status = OrderStatus::NEW;

        order2.clOrdID = "ORD_A2";
        order2.symbol = "IF2601";
        order2.side = OrderSide::SELL;
        order2.ordType = OrderType::LIMIT;
        order2.timeInForce = TimeInForce::DAY;
        order2.price = 4510.0;
        order2.orderQty = 5;
        order2.status = OrderStatus::NEW;

        order3.clOrdID = "ORD_B1";
        order3.symbol = "IC2601";
        order3.side = OrderSide::BUY;
        order3.ordType = OrderType::LIMIT;
        order3.timeInForce = TimeInForce::DAY;
        order3.price = 6000.0;
        order3.orderQty = 2;
        order3.status = OrderStatus::NEW;

        REQUIRE(store.saveOrder(order1));
        REQUIRE(store.saveOrder(order2));
        REQUIRE(store.saveOrder(order3));

        auto ifOrders = store.loadOrdersBySymbol("IF2601");
        REQUIRE(ifOrders.size() == 2);

        auto icOrders = store.loadOrdersBySymbol("IC2601");
        REQUIRE(icOrders.size() == 1);
    }

    SECTION("加载活跃订单") {
        Order active, filled, canceled;
        
        active.clOrdID = "ACTIVE";
        active.symbol = "IF2601";
        active.side = OrderSide::BUY;
        active.ordType = OrderType::LIMIT;
        active.timeInForce = TimeInForce::DAY;
        active.price = 4500.0;
        active.orderQty = 10;
        active.status = OrderStatus::NEW;

        filled.clOrdID = "FILLED";
        filled.symbol = "IF2601";
        filled.side = OrderSide::SELL;
        filled.ordType = OrderType::LIMIT;
        filled.timeInForce = TimeInForce::DAY;
        filled.price = 4510.0;
        filled.orderQty = 5;
        filled.cumQty = 5;
        filled.status = OrderStatus::FILLED;

        canceled.clOrdID = "CANCELED";
        canceled.symbol = "IF2601";
        canceled.side = OrderSide::BUY;
        canceled.ordType = OrderType::LIMIT;
        canceled.timeInForce = TimeInForce::DAY;
        canceled.price = 4490.0;
        canceled.orderQty = 3;
        canceled.status = OrderStatus::CANCELED;

        REQUIRE(store.saveOrder(active));
        REQUIRE(store.saveOrder(filled));
        REQUIRE(store.saveOrder(canceled));

        auto activeOrders = store.loadActiveOrders();
        REQUIRE(activeOrders.size() == 1);
        REQUIRE(activeOrders[0].clOrdID == "ACTIVE");
    }
}

TEST_CASE("SqliteStore - 成交存储", "[storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    // 先创建订单（外键约束要求）
    Order order;
    order.clOrdID = "ORD001";
    order.symbol = "IF2601";
    order.side = OrderSide::BUY;
    order.ordType = OrderType::LIMIT;
    order.timeInForce = TimeInForce::DAY;
    order.price = 4505.0;
    order.orderQty = 10;
    order.status = OrderStatus::NEW;
    REQUIRE(store.saveOrder(order));

    StoredTrade trade;
    trade.tradeId = "TRD001";
    trade.clOrdID = "ORD001";
    trade.symbol = "IF2601";
    trade.side = OrderSide::BUY;
    trade.price = 4505.0;
    trade.quantity = 5;
    trade.timestamp = 1702300000000;
    trade.counterpartyOrderId = "ORD002";

    REQUIRE(store.saveTrade(trade));

    auto trades = store.loadTradesByOrder("ORD001");
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].tradeId == "TRD001");
    REQUIRE(trades[0].price == 4505.0);
    REQUIRE(trades[0].quantity == 5);
}

TEST_CASE("SqliteStore - 会话状态存储", "[storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SessionState state;
    state.senderCompID = "SERVER";
    state.targetCompID = "CLIENT";
    state.sendSeqNum = 100;
    state.recvSeqNum = 50;
    state.lastUpdateTime = 1702300000000;

    REQUIRE(store.saveSessionState(state));

    auto loaded = store.loadSessionState("SERVER", "CLIENT");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sendSeqNum == 100);
    REQUIRE(loaded->recvSeqNum == 50);

    state.sendSeqNum = 101;
    state.recvSeqNum = 51;
    REQUIRE(store.saveSessionState(state));

    loaded = store.loadSessionState("SERVER", "CLIENT");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->sendSeqNum == 101);
    REQUIRE(loaded->recvSeqNum == 51);
}

TEST_CASE("SqliteStore - 消息存储", "[storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    for (int i = 1; i <= 10; ++i) {
        StoredMessage msg;
        msg.seqNum = i;
        msg.senderCompID = "SERVER";
        msg.targetCompID = "CLIENT";
        msg.msgType = "D";
        msg.rawMessage = "8=FIX.4.0|35=D|34=" + std::to_string(i) + "|";
        msg.timestamp = 1702300000000 + i * 1000;
        REQUIRE(store.saveMessage(msg));
    }

    auto messages = store.loadMessages("SERVER", "CLIENT", 3, 7);
    REQUIRE(messages.size() == 5);
    REQUIRE(messages[0].seqNum == 3);
    REQUIRE(messages[4].seqNum == 7);

    REQUIRE(store.deleteMessagesOlderThan(1702300005000));
    messages = store.loadMessages("SERVER", "CLIENT", 1, 10);
    REQUIRE(messages.size() == 6);
}

TEST_CASE("SqliteStore - 文件数据库", "[storage]") {
    std::string dbPath = "/tmp/test_fix_store_" + 
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".db";

    {
        SqliteStore store(dbPath);
        REQUIRE(store.isOpen());

        Order order;
        order.clOrdID = "PERSIST_TEST";
        order.symbol = "IF2601";
        order.side = OrderSide::BUY;
        order.ordType = OrderType::LIMIT;
        order.timeInForce = TimeInForce::DAY;
        order.price = 4500.0;
        order.orderQty = 10;
        order.status = OrderStatus::NEW;

        REQUIRE(store.saveOrder(order));
    }

    {
        SqliteStore store(dbPath);
        REQUIRE(store.isOpen());

        auto loaded = store.loadOrder("PERSIST_TEST");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->symbol == "IF2601");
    }

    std::filesystem::remove(dbPath);
}

// =============================================================================
// 账户存储测试
// =============================================================================

TEST_CASE("SqliteStore - 账户存储基本功能", "[storage][account]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SECTION("保存和加载账户") {
        Account account("user001", 1000000.0);
        account.frozenMargin = 50000.0;
        account.usedMargin = 100000.0;
        account.positionProfit = 20000.0;
        account.closeProfit = 5000.0;
        account.recalculateAvailable();

        REQUIRE(store.saveAccount(account));

        auto loaded = store.loadAccount("user001");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->accountId == "user001");
        REQUIRE(loaded->balance == Approx(1000000.0));
        REQUIRE(loaded->available == Approx(870000.0));
        REQUIRE(loaded->frozenMargin == Approx(50000.0));
        REQUIRE(loaded->usedMargin == Approx(100000.0));
        REQUIRE(loaded->positionProfit == Approx(20000.0));
        REQUIRE(loaded->closeProfit == Approx(5000.0));
    }

    SECTION("更新账户") {
        Account account("user002", 500000.0);
        REQUIRE(store.saveAccount(account));

        account.balance = 600000.0;
        account.usedMargin = 50000.0;
        account.recalculateAvailable();
        REQUIRE(store.saveAccount(account));

        auto loaded = store.loadAccount("user002");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->balance == Approx(600000.0));
        REQUIRE(loaded->usedMargin == Approx(50000.0));
    }

    SECTION("加载不存在的账户") {
        auto loaded = store.loadAccount("nonexistent");
        REQUIRE_FALSE(loaded.has_value());
    }

    SECTION("加载所有账户") {
        Account a1("user_a", 100000.0);
        Account a2("user_b", 200000.0);
        Account a3("user_c", 300000.0);

        REQUIRE(store.saveAccount(a1));
        REQUIRE(store.saveAccount(a2));
        REQUIRE(store.saveAccount(a3));

        auto accounts = store.loadAllAccounts();
        REQUIRE(accounts.size() == 3);
    }

    SECTION("删除账户") {
        Account account("user_delete", 100000.0);
        REQUIRE(store.saveAccount(account));

        auto loaded = store.loadAccount("user_delete");
        REQUIRE(loaded.has_value());

        REQUIRE(store.deleteAccount("user_delete"));

        loaded = store.loadAccount("user_delete");
        REQUIRE_FALSE(loaded.has_value());
    }
}

// =============================================================================
// 持仓存储测试
// =============================================================================

TEST_CASE("SqliteStore - 持仓存储基本功能", "[storage][position]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SECTION("保存和加载持仓") {
        Position pos("user001", "IF2601");
        pos.longPosition = 5;
        pos.longAvgPrice = 4000.0;
        pos.longProfit = 15000.0;
        pos.longMargin = 60000.0;
        pos.shortPosition = 2;
        pos.shortAvgPrice = 4100.0;
        pos.shortProfit = -6000.0;
        pos.shortMargin = 24600.0;

        REQUIRE(store.savePosition(pos));

        auto loaded = store.loadPosition("user001", "IF2601");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->accountId == "user001");
        REQUIRE(loaded->instrumentId == "IF2601");
        REQUIRE(loaded->longPosition == 5);
        REQUIRE(loaded->longAvgPrice == Approx(4000.0));
        REQUIRE(loaded->longProfit == Approx(15000.0));
        REQUIRE(loaded->longMargin == Approx(60000.0));
        REQUIRE(loaded->shortPosition == 2);
        REQUIRE(loaded->shortAvgPrice == Approx(4100.0));
        REQUIRE(loaded->shortProfit == Approx(-6000.0));
        REQUIRE(loaded->shortMargin == Approx(24600.0));
    }

    SECTION("更新持仓") {
        Position pos("user002", "IC2601");
        pos.longPosition = 3;
        pos.longAvgPrice = 6000.0;
        REQUIRE(store.savePosition(pos));

        pos.longPosition = 5;
        pos.longAvgPrice = 6100.0;
        pos.longProfit = 50000.0;
        REQUIRE(store.savePosition(pos));

        auto loaded = store.loadPosition("user002", "IC2601");
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->longPosition == 5);
        REQUIRE(loaded->longAvgPrice == Approx(6100.0));
        REQUIRE(loaded->longProfit == Approx(50000.0));
    }

    SECTION("加载不存在的持仓") {
        auto loaded = store.loadPosition("nonexistent", "IF2601");
        REQUIRE_FALSE(loaded.has_value());
    }

    SECTION("按账户加载持仓") {
        Position p1("user_multi", "IF2601");
        p1.longPosition = 2;
        Position p2("user_multi", "IC2601");
        p2.shortPosition = 3;
        Position p3("other_user", "IF2601");
        p3.longPosition = 1;

        REQUIRE(store.savePosition(p1));
        REQUIRE(store.savePosition(p2));
        REQUIRE(store.savePosition(p3));

        auto positions = store.loadPositionsByAccount("user_multi");
        REQUIRE(positions.size() == 2);
    }

    SECTION("加载所有持仓") {
        Position p1("user_a", "IF2601");
        Position p2("user_b", "IC2601");
        Position p3("user_c", "IH2601");

        REQUIRE(store.savePosition(p1));
        REQUIRE(store.savePosition(p2));
        REQUIRE(store.savePosition(p3));

        auto positions = store.loadAllPositions();
        REQUIRE(positions.size() == 3);
    }

    SECTION("删除单个持仓") {
        Position pos("user_del", "IF2601");
        REQUIRE(store.savePosition(pos));

        auto loaded = store.loadPosition("user_del", "IF2601");
        REQUIRE(loaded.has_value());

        REQUIRE(store.deletePosition("user_del", "IF2601"));

        loaded = store.loadPosition("user_del", "IF2601");
        REQUIRE_FALSE(loaded.has_value());
    }

    SECTION("删除账户所有持仓") {
        Position p1("user_del_all", "IF2601");
        Position p2("user_del_all", "IC2601");
        Position p3("user_keep", "IF2601");

        REQUIRE(store.savePosition(p1));
        REQUIRE(store.savePosition(p2));
        REQUIRE(store.savePosition(p3));

        REQUIRE(store.deletePositionsByAccount("user_del_all"));

        auto positions = store.loadPositionsByAccount("user_del_all");
        REQUIRE(positions.empty());

        auto kept = store.loadPosition("user_keep", "IF2601");
        REQUIRE(kept.has_value());
    }
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 13: 账户数据持久化round-trip**
 * **Validates: Requirements 2.4, 12.1**
 * 
 * 对于任意账户状态，保存到数据库后再加载，应得到等价的账户状态。
 */
TEST_CASE("SqliteStore - 账户数据持久化 round-trip 属性测试", "[storage][account][property]") {
    
    rc::prop("账户保存后加载应得到等价数据",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto account = *rc::gen::arbitrary<Account>();
            
            // 保存账户
            RC_ASSERT(store.saveAccount(account));
            
            // 加载账户
            auto loaded = store.loadAccount(account.accountId);
            RC_ASSERT(loaded.has_value());
            
            // 验证所有字段相等（不包括时间戳，因为时间戳精度可能有差异）
            RC_ASSERT(loaded->accountId == account.accountId);
            RC_ASSERT(loaded->balance == account.balance);
            RC_ASSERT(loaded->available == account.available);
            RC_ASSERT(loaded->frozenMargin == account.frozenMargin);
            RC_ASSERT(loaded->usedMargin == account.usedMargin);
            RC_ASSERT(loaded->positionProfit == account.positionProfit);
            RC_ASSERT(loaded->closeProfit == account.closeProfit);
        });
    
    rc::prop("多次保存同一账户应保持幂等性",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto account = *rc::gen::arbitrary<Account>();
            
            // 保存两次
            RC_ASSERT(store.saveAccount(account));
            RC_ASSERT(store.saveAccount(account));
            
            // 应该只有一条记录
            auto accounts = store.loadAllAccounts();
            int count = 0;
            for (const auto& a : accounts) {
                if (a.accountId == account.accountId) {
                    count++;
                }
            }
            RC_ASSERT(count == 1);
        });
}

/**
 * **Feature: paper-trading-system, Property 14: 持仓数据持久化round-trip**
 * **Validates: Requirements 7.5, 12.2**
 * 
 * 对于任意持仓状态，保存到数据库后再加载，应得到等价的持仓状态。
 */
TEST_CASE("SqliteStore - 持仓数据持久化 round-trip 属性测试", "[storage][position][property]") {
    
    rc::prop("持仓保存后加载应得到等价数据",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto position = *rc::gen::arbitrary<Position>();
            
            // 保存持仓
            RC_ASSERT(store.savePosition(position));
            
            // 加载持仓
            auto loaded = store.loadPosition(position.accountId, position.instrumentId);
            RC_ASSERT(loaded.has_value());
            
            // 验证所有字段相等（不包括时间戳）
            RC_ASSERT(loaded->accountId == position.accountId);
            RC_ASSERT(loaded->instrumentId == position.instrumentId);
            RC_ASSERT(loaded->longPosition == position.longPosition);
            RC_ASSERT(loaded->longAvgPrice == position.longAvgPrice);
            RC_ASSERT(loaded->longProfit == position.longProfit);
            RC_ASSERT(loaded->longMargin == position.longMargin);
            RC_ASSERT(loaded->shortPosition == position.shortPosition);
            RC_ASSERT(loaded->shortAvgPrice == position.shortAvgPrice);
            RC_ASSERT(loaded->shortProfit == position.shortProfit);
            RC_ASSERT(loaded->shortMargin == position.shortMargin);
        });
    
    rc::prop("多次保存同一持仓应保持幂等性",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto position = *rc::gen::arbitrary<Position>();
            
            // 保存两次
            RC_ASSERT(store.savePosition(position));
            RC_ASSERT(store.savePosition(position));
            
            // 应该只有一条记录
            auto positions = store.loadAllPositions();
            int count = 0;
            for (const auto& p : positions) {
                if (p.accountId == position.accountId && 
                    p.instrumentId == position.instrumentId) {
                    count++;
                }
            }
            RC_ASSERT(count == 1);
        });
    
    rc::prop("删除持仓后应无法加载",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto position = *rc::gen::arbitrary<Position>();
            
            // 保存
            RC_ASSERT(store.savePosition(position));
            
            // 验证存在
            auto loaded = store.loadPosition(position.accountId, position.instrumentId);
            RC_ASSERT(loaded.has_value());
            
            // 删除
            RC_ASSERT(store.deletePosition(position.accountId, position.instrumentId));
            
            // 验证不存在
            loaded = store.loadPosition(position.accountId, position.instrumentId);
            RC_ASSERT(!loaded.has_value());
        });
}
