#include "../catch2/catch.hpp"
#include "app/engine/order_book.hpp"

using namespace fix40;

// 辅助函数：创建测试订单
Order createOrder(const std::string& clOrdID, OrderSide side, double price, int64_t qty, 
                  const std::string& symbol = "TEST") {
    Order order;
    order.clOrdID = clOrdID;
    order.symbol = symbol;
    order.side = side;
    order.ordType = OrderType::LIMIT;
    order.price = price;
    order.orderQty = qty;
    order.leavesQty = qty;
    order.status = OrderStatus::PENDING_NEW;
    return order;
}

TEST_CASE("OrderBook - Construction", "[order_book]") {
    OrderBook book("IF2401");
    
    REQUIRE(book.getSymbol() == "IF2401");
    REQUIRE(book.empty());
    REQUIRE(book.getBidOrderCount() == 0);
    REQUIRE(book.getAskOrderCount() == 0);
    REQUIRE_FALSE(book.getBestBid().has_value());
    REQUIRE_FALSE(book.getBestAsk().has_value());
}

TEST_CASE("OrderBook - Add single buy order (no match)", "[order_book]") {
    OrderBook book("TEST");
    
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());  // 无对手盘，不成交
    REQUIRE_FALSE(buyOrder.orderID.empty());  // 应该生成 OrderID
    REQUIRE(buyOrder.status == OrderStatus::NEW);
    REQUIRE(book.getBidOrderCount() == 1);
    REQUIRE(book.getAskOrderCount() == 0);
    REQUIRE(book.getBestBid().value() == 100.0);
}

TEST_CASE("OrderBook - Add single sell order (no match)", "[order_book]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 101.0, 10);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(sellOrder.status == OrderStatus::NEW);
    REQUIRE(book.getBidOrderCount() == 0);
    REQUIRE(book.getAskOrderCount() == 1);
    REQUIRE(book.getBestAsk().value() == 101.0);
}

TEST_CASE("OrderBook - Full match (buy crosses sell)", "[order_book]") {
    OrderBook book("TEST");
    
    // 先挂卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // 买单价格 >= 卖单价格，应该成交
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 10);
    REQUIRE(trades[0].price == 100.0);  // 成交价取被动方（卖单）价格
    REQUIRE(trades[0].buyClOrdID == "BUY001");
    REQUIRE(trades[0].sellClOrdID == "SELL001");
    
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(buyOrder.cumQty == 10);
    REQUIRE(buyOrder.leavesQty == 0);
    
    REQUIRE(book.empty());  // 双方都全部成交
}

TEST_CASE("OrderBook - Full match (sell crosses buy)", "[order_book]") {
    OrderBook book("TEST");
    
    // 先挂买单
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    // 卖单价格 <= 买单价格，应该成交
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 10);
    REQUIRE(trades[0].price == 100.0);  // 成交价取被动方（买单）价格
    
    REQUIRE(sellOrder.status == OrderStatus::FILLED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Partial match", "[order_book]") {
    OrderBook book("TEST");
    
    // 挂卖单 10 手
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // 买单 15 手，只能成交 10 手
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 15);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 10);
    
    REQUIRE(buyOrder.status == OrderStatus::PARTIALLY_FILLED);
    REQUIRE(buyOrder.cumQty == 10);
    REQUIRE(buyOrder.leavesQty == 5);
    
    // 剩余 5 手应该挂入买盘
    REQUIRE(book.getBidOrderCount() == 1);
    REQUIRE(book.getAskOrderCount() == 0);
}

TEST_CASE("OrderBook - Price priority", "[order_book]") {
    OrderBook book("TEST");
    
    // 挂两个卖单，价格不同
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 101.0, 10);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 100.0, 10);  // 更低价
    book.addOrder(sell1);
    book.addOrder(sell2);
    
    // 买单应该先与低价卖单成交
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 101.0, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].sellClOrdID == "SELL002");  // 低价优先
    REQUIRE(trades[0].price == 100.0);
    
    REQUIRE(book.getAskOrderCount() == 1);
    REQUIRE(book.getBestAsk().value() == 101.0);  // 剩余高价卖单
}

TEST_CASE("OrderBook - Time priority (same price)", "[order_book]") {
    OrderBook book("TEST");
    
    // 挂两个同价卖单
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 100.0, 10);
    book.addOrder(sell1);
    book.addOrder(sell2);
    
    // 买单应该先与先挂的卖单成交
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].sellClOrdID == "SELL001");  // 时间优先
    
    REQUIRE(book.getAskOrderCount() == 1);
}

TEST_CASE("OrderBook - Multiple trades in one order", "[order_book]") {
    OrderBook book("TEST");
    
    // 挂多个卖单
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 100.5, 5);
    Order sell3 = createOrder("SELL003", OrderSide::SELL, 101.0, 5);
    book.addOrder(sell1);
    book.addOrder(sell2);
    book.addOrder(sell3);
    
    // 大买单扫多个价位
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 101.0, 12);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 3);
    REQUIRE(trades[0].price == 100.0);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(trades[1].price == 100.5);
    REQUIRE(trades[1].qty == 5);
    REQUIRE(trades[2].price == 101.0);
    REQUIRE(trades[2].qty == 2);
    
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(buyOrder.cumQty == 12);
    
    // 卖盘应该剩余 3 手 @ 101.0
    REQUIRE(book.getAskOrderCount() == 1);
}

TEST_CASE("OrderBook - No match (price gap)", "[order_book]") {
    OrderBook book("TEST");
    
    // 买盘 100，卖盘 101，有价差
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 101.0, 10);
    
    book.addOrder(buyOrder);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.empty());  // 不成交
    REQUIRE(book.getBidOrderCount() == 1);
    REQUIRE(book.getAskOrderCount() == 1);
    REQUIRE(book.getBestBid().value() == 100.0);
    REQUIRE(book.getBestAsk().value() == 101.0);
}

TEST_CASE("OrderBook - Cancel order", "[order_book]") {
    OrderBook book("TEST");
    
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    REQUIRE(book.getBidOrderCount() == 1);
    
    auto canceled = book.cancelOrder("BUY001");
    
    REQUIRE(canceled.has_value());
    REQUIRE(canceled->clOrdID == "BUY001");
    REQUIRE(canceled->status == OrderStatus::CANCELED);
    REQUIRE(book.getBidOrderCount() == 0);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Cancel non-existent order", "[order_book]") {
    OrderBook book("TEST");
    
    auto canceled = book.cancelOrder("NONEXISTENT");
    
    REQUIRE_FALSE(canceled.has_value());
}

TEST_CASE("OrderBook - Find order", "[order_book]") {
    OrderBook book("TEST");
    
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    const Order* found = book.findOrder("BUY001");
    REQUIRE(found != nullptr);
    REQUIRE(found->clOrdID == "BUY001");
    
    const Order* notFound = book.findOrder("NONEXISTENT");
    REQUIRE(notFound == nullptr);
}

TEST_CASE("OrderBook - Get bid/ask levels", "[order_book]") {
    OrderBook book("TEST");
    
    // 添加多个价位
    Order buy1 = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    Order buy2 = createOrder("BUY002", OrderSide::BUY, 99.5, 20);
    Order buy3 = createOrder("BUY003", OrderSide::BUY, 99.0, 30);
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 101.0, 15);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 101.5, 25);
    
    book.addOrder(buy1);
    book.addOrder(buy2);
    book.addOrder(buy3);
    book.addOrder(sell1);
    book.addOrder(sell2);
    
    auto bidLevels = book.getBidLevels(5);
    REQUIRE(bidLevels.size() == 3);
    REQUIRE(bidLevels[0].price == 100.0);  // 最高买价
    REQUIRE(bidLevels[0].totalQty == 10);
    REQUIRE(bidLevels[1].price == 99.5);
    REQUIRE(bidLevels[2].price == 99.0);
    
    auto askLevels = book.getAskLevels(5);
    REQUIRE(askLevels.size() == 2);
    REQUIRE(askLevels[0].price == 101.0);  // 最低卖价
    REQUIRE(askLevels[0].totalQty == 15);
    REQUIRE(askLevels[1].price == 101.5);
}

TEST_CASE("OrderBook - Average price calculation", "[order_book]") {
    OrderBook book("TEST");
    
    // 挂两个不同价格的卖单
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 102.0, 10);
    book.addOrder(sell1);
    book.addOrder(sell2);
    
    // 买单扫两个价位
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 102.0, 20);
    book.addOrder(buyOrder);
    
    // 平均价 = (100*10 + 102*10) / 20 = 101
    REQUIRE(buyOrder.avgPx == Approx(101.0));
}

TEST_CASE("OrderBook - Order ID generation", "[order_book]") {
    OrderBook book("IF2401");
    
    Order order1 = createOrder("O1", OrderSide::BUY, 100.0, 10, "IF2401");
    Order order2 = createOrder("O2", OrderSide::BUY, 100.0, 10, "IF2401");
    
    book.addOrder(order1);
    book.addOrder(order2);
    
    REQUIRE(order1.orderID != order2.orderID);
    REQUIRE(order1.orderID.find("IF2401") != std::string::npos);
}

TEST_CASE("OrderBook - Input validation", "[order_book]") {
    OrderBook book("TEST");
    
    SECTION("Reject negative quantity") {
        Order order = createOrder("O1", OrderSide::BUY, 100.0, -10);
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
        REQUIRE(book.empty());
    }
    
    SECTION("Reject zero quantity") {
        Order order = createOrder("O1", OrderSide::BUY, 100.0, 0);
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
    }
    
    SECTION("Reject negative price for limit order") {
        Order order = createOrder("O1", OrderSide::BUY, -100.0, 10);
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
    }
    
    SECTION("Reject zero price for limit order") {
        Order order = createOrder("O1", OrderSide::BUY, 0.0, 10);
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
    }
    
    SECTION("Reject symbol mismatch") {
        Order order = createOrder("O1", OrderSide::BUY, 100.0, 10, "OTHER");
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
    }
    
    SECTION("Reject empty clOrdID") {
        Order order = createOrder("", OrderSide::BUY, 100.0, 10);
        auto trades = book.addOrder(order);
        
        REQUIRE(trades.empty());
        REQUIRE(order.status == OrderStatus::REJECTED);
    }
}


// ============================================================================
// 市价单测试
// ============================================================================

// 辅助函数：创建市价单
Order createMarketOrder(const std::string& clOrdID, OrderSide side, int64_t qty,
                        const std::string& symbol = "TEST") {
    Order order;
    order.clOrdID = clOrdID;
    order.symbol = symbol;
    order.side = side;
    order.ordType = OrderType::MARKET;
    order.price = 0.0;  // 市价单不需要价格
    order.orderQty = qty;
    order.leavesQty = qty;
    order.status = OrderStatus::PENDING_NEW;
    return order;
}

TEST_CASE("OrderBook - Market order full match", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 先挂限价卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // 市价买单应该直接成交
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 10);
    REQUIRE(trades[0].price == 100.0);  // 成交价取对手盘价格
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(buyOrder.cumQty == 10);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Market sell order full match", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 先挂限价买单
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    // 市价卖单应该直接成交
    Order sellOrder = createMarketOrder("SELL001", OrderSide::SELL, 10);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 10);
    REQUIRE(trades[0].price == 100.0);
    REQUIRE(sellOrder.status == OrderStatus::FILLED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Market order sweeps multiple price levels", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 挂多个价位的卖单
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 101.0, 5);
    Order sell3 = createOrder("SELL003", OrderSide::SELL, 102.0, 5);
    book.addOrder(sell1);
    book.addOrder(sell2);
    book.addOrder(sell3);
    
    // 市价买单扫所有价位
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 15);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 3);
    REQUIRE(trades[0].price == 100.0);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(trades[1].price == 101.0);
    REQUIRE(trades[1].qty == 5);
    REQUIRE(trades[2].price == 102.0);
    REQUIRE(trades[2].qty == 5);
    
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(buyOrder.cumQty == 15);
    // 平均价 = (100*5 + 101*5 + 102*5) / 15 = 101
    REQUIRE(buyOrder.avgPx == Approx(101.0));
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Market order partial fill then cancel", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 只挂 5 手卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    book.addOrder(sellOrder);
    
    // 市价买单 10 手，只能成交 5 手，剩余取消
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(buyOrder.cumQty == 5);
    REQUIRE(buyOrder.leavesQty == 5);
    REQUIRE(buyOrder.status == OrderStatus::CANCELED);  // 剩余部分取消
    REQUIRE(book.empty());  // 市价单不挂入订单簿
}

TEST_CASE("OrderBook - Market order rejected when no counterparty", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 空订单簿，市价买单应该被拒绝
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 10);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - Market sell order rejected when no bids", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 只有卖盘，没有买盘
    Order sellLimit = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellLimit);
    
    // 市价卖单应该被拒绝（没有买盘）
    Order sellMarket = createMarketOrder("SELL002", OrderSide::SELL, 10);
    auto trades = book.addOrder(sellMarket);
    
    REQUIRE(trades.empty());
    REQUIRE(sellMarket.status == OrderStatus::REJECTED);
    REQUIRE(book.getAskOrderCount() == 1);  // 原卖单还在
}

TEST_CASE("OrderBook - Market order does not enter book", "[order_book][market]") {
    OrderBook book("TEST");
    
    // 挂少量卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 3);
    book.addOrder(sellOrder);
    
    // 市价买单部分成交
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 10);
    book.addOrder(buyOrder);
    
    // 市价单不应该挂入订单簿
    REQUIRE(book.getBidOrderCount() == 0);
    REQUIRE(book.findOrder("BUY001") == nullptr);
}

TEST_CASE("OrderBook - Market order price is 0 but still valid", "[order_book][market]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // 市价单 price=0 是合法的
    Order buyOrder = createMarketOrder("BUY001", OrderSide::BUY, 10);
    REQUIRE(buyOrder.price == 0.0);
    
    auto trades = book.addOrder(buyOrder);
    REQUIRE(trades.size() == 1);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
}


// ============================================================================
// IOC/FOK 订单测试
// ============================================================================

// 辅助函数：创建带 TimeInForce 的订单
Order createOrderWithTIF(const std::string& clOrdID, OrderSide side, double price, 
                         int64_t qty, TimeInForce tif, const std::string& symbol = "TEST") {
    Order order = createOrder(clOrdID, side, price, qty, symbol);
    order.timeInForce = tif;
    return order;
}

TEST_CASE("OrderBook - IOC order full match", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 先挂卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // IOC 买单完全成交
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - IOC order partial fill then cancel", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 只挂 5 手卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    book.addOrder(sellOrder);
    
    // IOC 买单 10 手，只能成交 5 手，剩余取消
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(buyOrder.cumQty == 5);
    REQUIRE(buyOrder.leavesQty == 5);
    REQUIRE(buyOrder.status == OrderStatus::CANCELED);  // 剩余取消
    REQUIRE(book.getBidOrderCount() == 0);  // IOC 不挂入订单簿
}

TEST_CASE("OrderBook - IOC order no match rejected", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 卖盘价格高于买单价格，无法成交
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 101.0, 10);
    book.addOrder(sellOrder);
    
    // IOC 买单无法成交，被拒绝
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(book.getBidOrderCount() == 0);  // IOC 不挂入订单簿
}

TEST_CASE("OrderBook - IOC order does not enter book", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 空订单簿，IOC 买单无法成交
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(book.empty());  // IOC 不挂入订单簿
}

TEST_CASE("OrderBook - FOK order full match", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 挂足够的卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // FOK 买单完全成交
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - FOK order rejected when cannot fill completely", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 只挂 5 手卖单
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    book.addOrder(sellOrder);
    
    // FOK 买单 10 手，无法全部成交，被拒绝
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());  // 没有成交
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(buyOrder.cumQty == 0);
    REQUIRE(book.getAskOrderCount() == 1);  // 原卖单还在
}

TEST_CASE("OrderBook - FOK order rejected when no counterparty", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 空订单簿
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(book.empty());
}

TEST_CASE("OrderBook - FOK order rejected when price gap", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 卖盘价格高于买单价格
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 101.0, 10);
    book.addOrder(sellOrder);
    
    // FOK 买单价格低于卖盘，无法成交
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
}

TEST_CASE("OrderBook - FOK order sweeps multiple levels", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 挂多个价位的卖单，总量足够
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 100.5, 5);
    Order sell3 = createOrder("SELL003", OrderSide::SELL, 101.0, 5);
    book.addOrder(sell1);
    book.addOrder(sell2);
    book.addOrder(sell3);
    
    // FOK 买单扫多个价位
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 101.0, 12, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 3);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
    REQUIRE(buyOrder.cumQty == 12);
}

TEST_CASE("OrderBook - FOK sell order", "[order_book][fok]") {
    OrderBook book("TEST");
    
    // 挂买单
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    // FOK 卖单
    Order sellOrder = createOrderWithTIF("SELL001", OrderSide::SELL, 100.0, 10, TimeInForce::FOK);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(sellOrder.status == OrderStatus::FILLED);
}

TEST_CASE("OrderBook - DAY order enters book when no match", "[order_book][tif]") {
    OrderBook book("TEST");
    
    // DAY 订单无法成交时应该挂入订单簿
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::DAY);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::NEW);
    REQUIRE(book.getBidOrderCount() == 1);  // DAY 挂入订单簿
}

TEST_CASE("OrderBook - GTC order enters book when no match", "[order_book][tif]") {
    OrderBook book("TEST");
    
    // GTC 订单无法成交时应该挂入订单簿
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 100.0, 10, TimeInForce::GTC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::NEW);
    REQUIRE(book.getBidOrderCount() == 1);  // GTC 挂入订单簿
}


// ============================================================================
// 组合订单类型测试
// ============================================================================

// 辅助函数：创建市价单 + TimeInForce
Order createMarketOrderWithTIF(const std::string& clOrdID, OrderSide side, int64_t qty,
                               TimeInForce tif, const std::string& symbol = "TEST") {
    Order order = createMarketOrder(clOrdID, side, qty, symbol);
    order.timeInForce = tif;
    return order;
}

TEST_CASE("OrderBook - Market IOC order full match", "[order_book][market][ioc]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // Market + IOC 完全成交
    Order buyOrder = createMarketOrderWithTIF("BUY001", OrderSide::BUY, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
}

TEST_CASE("OrderBook - Market IOC order partial fill then cancel", "[order_book][market][ioc]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    book.addOrder(sellOrder);
    
    // Market + IOC 部分成交后取消
    Order buyOrder = createMarketOrderWithTIF("BUY001", OrderSide::BUY, 10, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(buyOrder.cumQty == 5);
    REQUIRE(buyOrder.status == OrderStatus::CANCELED);
}

TEST_CASE("OrderBook - Market FOK order full match", "[order_book][market][fok]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 10);
    book.addOrder(sellOrder);
    
    // Market + FOK 完全成交
    Order buyOrder = createMarketOrderWithTIF("BUY001", OrderSide::BUY, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(buyOrder.status == OrderStatus::FILLED);
}

TEST_CASE("OrderBook - Market FOK order rejected when insufficient liquidity", "[order_book][market][fok]") {
    OrderBook book("TEST");
    
    Order sellOrder = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    book.addOrder(sellOrder);
    
    // Market + FOK 流动性不足，拒绝
    Order buyOrder = createMarketOrderWithTIF("BUY001", OrderSide::BUY, 10, TimeInForce::FOK);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.empty());
    REQUIRE(buyOrder.status == OrderStatus::REJECTED);
    REQUIRE(book.getAskOrderCount() == 1);  // 原卖单还在
}

TEST_CASE("OrderBook - IOC sell order full match", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 10);
    book.addOrder(buyOrder);
    
    // IOC 卖单完全成交
    Order sellOrder = createOrderWithTIF("SELL001", OrderSide::SELL, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(sellOrder.status == OrderStatus::FILLED);
}

TEST_CASE("OrderBook - IOC sell order partial fill then cancel", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    Order buyOrder = createOrder("BUY001", OrderSide::BUY, 100.0, 5);
    book.addOrder(buyOrder);
    
    // IOC 卖单部分成交后取消
    Order sellOrder = createOrderWithTIF("SELL001", OrderSide::SELL, 100.0, 10, TimeInForce::IOC);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].qty == 5);
    REQUIRE(sellOrder.cumQty == 5);
    REQUIRE(sellOrder.status == OrderStatus::CANCELED);
    REQUIRE(book.getAskOrderCount() == 0);  // IOC 不挂入订单簿
}

TEST_CASE("OrderBook - IOC order sweeps multiple price levels", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 挂多个价位的卖单
    Order sell1 = createOrder("SELL001", OrderSide::SELL, 100.0, 5);
    Order sell2 = createOrder("SELL002", OrderSide::SELL, 100.5, 5);
    Order sell3 = createOrder("SELL003", OrderSide::SELL, 101.0, 5);
    book.addOrder(sell1);
    book.addOrder(sell2);
    book.addOrder(sell3);
    
    // IOC 买单扫多个价位，但数量超过可用量
    Order buyOrder = createOrderWithTIF("BUY001", OrderSide::BUY, 101.0, 20, TimeInForce::IOC);
    auto trades = book.addOrder(buyOrder);
    
    REQUIRE(trades.size() == 3);
    REQUIRE(trades[0].price == 100.0);
    REQUIRE(trades[1].price == 100.5);
    REQUIRE(trades[2].price == 101.0);
    REQUIRE(buyOrder.cumQty == 15);
    REQUIRE(buyOrder.leavesQty == 5);
    REQUIRE(buyOrder.status == OrderStatus::CANCELED);  // 剩余取消
    REQUIRE(book.empty());  // 所有卖单都被吃掉
}

TEST_CASE("OrderBook - IOC sell order sweeps multiple price levels", "[order_book][ioc]") {
    OrderBook book("TEST");
    
    // 挂多个价位的买单
    Order buy1 = createOrder("BUY001", OrderSide::BUY, 101.0, 5);
    Order buy2 = createOrder("BUY002", OrderSide::BUY, 100.5, 5);
    Order buy3 = createOrder("BUY003", OrderSide::BUY, 100.0, 5);
    book.addOrder(buy1);
    book.addOrder(buy2);
    book.addOrder(buy3);
    
    // IOC 卖单扫多个价位
    Order sellOrder = createOrderWithTIF("SELL001", OrderSide::SELL, 100.0, 12, TimeInForce::IOC);
    auto trades = book.addOrder(sellOrder);
    
    REQUIRE(trades.size() == 3);
    REQUIRE(trades[0].price == 101.0);  // 最高买价优先
    REQUIRE(trades[1].price == 100.5);
    REQUIRE(trades[2].price == 100.0);
    REQUIRE(sellOrder.cumQty == 12);
    REQUIRE(sellOrder.status == OrderStatus::FILLED);
}
