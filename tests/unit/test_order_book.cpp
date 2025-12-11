#include "../catch2/catch.hpp"
#include "app/order_book.hpp"

using namespace fix40;

// 辅助函数：创建测试订单
Order createOrder(const std::string& clOrdID, OrderSide side, double price, int64_t qty) {
    Order order;
    order.clOrdID = clOrdID;
    order.symbol = "TEST";
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
    
    Order order1 = createOrder("O1", OrderSide::BUY, 100.0, 10);
    Order order2 = createOrder("O2", OrderSide::BUY, 100.0, 10);
    
    book.addOrder(order1);
    book.addOrder(order2);
    
    REQUIRE(order1.orderID != order2.orderID);
    REQUIRE(order1.orderID.find("IF2401") != std::string::npos);
}
