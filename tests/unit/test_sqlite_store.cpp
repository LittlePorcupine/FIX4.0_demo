#include "catch2/catch.hpp"
#include "storage/sqlite_store.hpp"
#include <filesystem>

using namespace fix40;

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

        order.cumQty = 3;
        order.leavesQty = 2;
        order.avgPx = 4510.0;
        order.status = OrderStatus::PARTIALLY_FILLED;
        REQUIRE(store.updateOrder(order));

        auto loaded = store.loadOrder("ORD002");
        REQUIRE(loaded.has_value());
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
