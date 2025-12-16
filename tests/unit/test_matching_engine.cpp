/**
 * @file test_matching_engine.cpp
 * @brief 撮合引擎属性测试
 *
 * 测试行情驱动撮合引擎的核心功能，包括：
 * - Property 2: 限价单撮合正确性
 * - Property 3: 行情驱动撮合正确性
 * - Property 4: 市价单撮合正确性
 *
 * **Validates: Requirements 4.1-4.6, 5.1, 5.2**
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/matching_engine.hpp"
#include "app/market_data_snapshot.hpp"
#include "app/order.hpp"
#include "market/market_data.hpp"

using namespace fix40;

// =============================================================================
// 辅助函数（匿名命名空间避免符号冲突）
// =============================================================================

namespace {

/**
 * @brief 创建测试用限价单
 */
Order makeTestLimitOrder(const std::string& clOrdID, OrderSide side, 
                         double price, int64_t qty, const std::string& symbol = "IF2601") {
    Order order;
    order.clOrdID = clOrdID;
    order.symbol = symbol;
    order.side = side;
    order.ordType = OrderType::LIMIT;
    order.price = price;
    order.orderQty = qty;
    order.leavesQty = qty;
    order.status = OrderStatus::PENDING_NEW;
    order.timeInForce = TimeInForce::DAY;
    return order;
}

/**
 * @brief 创建测试用市价单
 */
Order makeTestMarketOrder(const std::string& clOrdID, OrderSide side, 
                          int64_t qty, const std::string& symbol = "IF2601") {
    Order order;
    order.clOrdID = clOrdID;
    order.symbol = symbol;
    order.side = side;
    order.ordType = OrderType::MARKET;
    order.price = 0.0;
    order.orderQty = qty;
    order.leavesQty = qty;
    order.status = OrderStatus::PENDING_NEW;
    order.timeInForce = TimeInForce::DAY;
    return order;
}

/**
 * @brief 创建测试用行情快照
 */
MarketDataSnapshot makeTestSnapshot(const std::string& instrumentId,
                                    double bidPrice, int32_t bidVolume,
                                    double askPrice, int32_t askVolume) {
    MarketDataSnapshot snapshot(instrumentId);
    snapshot.bidPrice1 = bidPrice;
    snapshot.bidVolume1 = bidVolume;
    snapshot.askPrice1 = askPrice;
    snapshot.askVolume1 = askVolume;
    snapshot.lastPrice = (bidPrice + askPrice) / 2.0;
    snapshot.upperLimitPrice = askPrice * 1.1;
    snapshot.lowerLimitPrice = bidPrice * 0.9;
    return snapshot;
}

} // anonymous namespace

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("MatchingEngine - canMatchBuyOrder 基本测试", "[matching_engine]") {
    MatchingEngine engine;
    
    SECTION("买价 >= 卖一价时应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        
        // 买价等于卖一价
        auto order1 = makeTestLimitOrder("O1", OrderSide::BUY, 4001.0, 1);
        REQUIRE(engine.canMatchBuyOrder(order1, snapshot) == true);
        
        // 买价高于卖一价
        auto order2 = makeTestLimitOrder("O2", OrderSide::BUY, 4002.0, 1);
        REQUIRE(engine.canMatchBuyOrder(order2, snapshot) == true);
    }
    
    SECTION("买价 < 卖一价时不应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        
        auto order = makeTestLimitOrder("O1", OrderSide::BUY, 4000.5, 1);
        REQUIRE(engine.canMatchBuyOrder(order, snapshot) == false);
    }
    
    SECTION("无卖盘时不应成交") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 4000.0;
        snapshot.bidVolume1 = 100;
        snapshot.askPrice1 = 0.0;  // 无卖盘
        snapshot.askVolume1 = 0;
        
        auto order = makeTestLimitOrder("O1", OrderSide::BUY, 5000.0, 1);
        REQUIRE(engine.canMatchBuyOrder(order, snapshot) == false);
    }
}

TEST_CASE("MatchingEngine - canMatchSellOrder 基本测试", "[matching_engine]") {
    MatchingEngine engine;
    
    SECTION("卖价 <= 买一价时应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        
        // 卖价等于买一价
        auto order1 = makeTestLimitOrder("O1", OrderSide::SELL, 4000.0, 1);
        REQUIRE(engine.canMatchSellOrder(order1, snapshot) == true);
        
        // 卖价低于买一价
        auto order2 = makeTestLimitOrder("O2", OrderSide::SELL, 3999.0, 1);
        REQUIRE(engine.canMatchSellOrder(order2, snapshot) == true);
    }
    
    SECTION("卖价 > 买一价时不应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        
        auto order = makeTestLimitOrder("O1", OrderSide::SELL, 4000.5, 1);
        REQUIRE(engine.canMatchSellOrder(order, snapshot) == false);
    }
    
    SECTION("无买盘时不应成交") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 0.0;  // 无买盘
        snapshot.bidVolume1 = 0;
        snapshot.askPrice1 = 4001.0;
        snapshot.askVolume1 = 50;
        
        auto order = makeTestLimitOrder("O1", OrderSide::SELL, 3000.0, 1);
        REQUIRE(engine.canMatchSellOrder(order, snapshot) == false);
    }
}

TEST_CASE("MatchingEngine - 市价单撮合条件", "[matching_engine]") {
    MatchingEngine engine;
    
    SECTION("市价买单有卖盘时应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        auto order = makeTestMarketOrder("O1", OrderSide::BUY, 1);
        REQUIRE(engine.canMatchBuyOrder(order, snapshot) == true);
    }
    
    SECTION("市价卖单有买盘时应成交") {
        auto snapshot = makeTestSnapshot("IF2601", 4000.0, 100, 4001.0, 50);
        auto order = makeTestMarketOrder("O1", OrderSide::SELL, 1);
        REQUIRE(engine.canMatchSellOrder(order, snapshot) == true);
    }
    
    SECTION("市价买单无卖盘时不应成交") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 4000.0;
        snapshot.bidVolume1 = 100;
        snapshot.askPrice1 = 0.0;
        snapshot.askVolume1 = 0;
        
        auto order = makeTestMarketOrder("O1", OrderSide::BUY, 1);
        REQUIRE(engine.canMatchBuyOrder(order, snapshot) == false);
    }
    
    SECTION("市价卖单无买盘时不应成交") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 0.0;
        snapshot.bidVolume1 = 0;
        snapshot.askPrice1 = 4001.0;
        snapshot.askVolume1 = 50;
        
        auto order = makeTestMarketOrder("O1", OrderSide::SELL, 1);
        REQUIRE(engine.canMatchSellOrder(order, snapshot) == false);
    }
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 2: 限价单撮合正确性**
 * **Validates: Requirements 4.1, 4.2, 4.3, 4.4**
 *
 * *对于任意* 限价买单和行情快照，当买价 >= 卖一价时应成交，
 * 当买价 < 卖一价时应挂单等待。卖单同理。
 */
TEST_CASE("Property 2: 限价单撮合正确性", "[matching_engine][property]") {
    // 注意：属性测试中每次迭代都创建新的 engine 实例，
    // 因此这里不需要外部 engine 变量
    
    rc::prop("买单：买价 >= 卖一价 当且仅当 可成交",
        []() {
            // 生成有效的价格和数量
            auto askPrice = *rc::gen::inRange(1000, 10000);
            auto bidPrice = *rc::gen::inRange(1000, 10000);
            auto buyPrice = *rc::gen::inRange(1000, 10000);
            auto askVolume = *rc::gen::inRange(1, 1000);
            auto bidVolume = *rc::gen::inRange(1, 1000);
            
            // 确保卖盘有效
            RC_PRE(askPrice > 0 && askVolume > 0);
            
            MarketDataSnapshot snapshot("IF2601");
            snapshot.bidPrice1 = static_cast<double>(bidPrice);
            snapshot.bidVolume1 = bidVolume;
            snapshot.askPrice1 = static_cast<double>(askPrice);
            snapshot.askVolume1 = askVolume;
            
            Order order;
            order.clOrdID = "TEST";
            order.symbol = "IF2601";
            order.side = OrderSide::BUY;
            order.ordType = OrderType::LIMIT;
            order.price = static_cast<double>(buyPrice);
            order.orderQty = 1;
            
            MatchingEngine eng;
            bool canMatch = eng.canMatchBuyOrder(order, snapshot);
            bool shouldMatch = (buyPrice >= askPrice);
            
            RC_ASSERT(canMatch == shouldMatch);
        });
    
    rc::prop("卖单：卖价 <= 买一价 当且仅当 可成交",
        []() {
            auto askPrice = *rc::gen::inRange(1000, 10000);
            auto bidPrice = *rc::gen::inRange(1000, 10000);
            auto sellPrice = *rc::gen::inRange(1000, 10000);
            auto askVolume = *rc::gen::inRange(1, 1000);
            auto bidVolume = *rc::gen::inRange(1, 1000);
            
            // 确保买盘有效
            RC_PRE(bidPrice > 0 && bidVolume > 0);
            
            MarketDataSnapshot snapshot("IF2601");
            snapshot.bidPrice1 = static_cast<double>(bidPrice);
            snapshot.bidVolume1 = bidVolume;
            snapshot.askPrice1 = static_cast<double>(askPrice);
            snapshot.askVolume1 = askVolume;
            
            Order order;
            order.clOrdID = "TEST";
            order.symbol = "IF2601";
            order.side = OrderSide::SELL;
            order.ordType = OrderType::LIMIT;
            order.price = static_cast<double>(sellPrice);
            order.orderQty = 1;
            
            MatchingEngine eng;
            bool canMatch = eng.canMatchSellOrder(order, snapshot);
            bool shouldMatch = (sellPrice <= bidPrice);
            
            RC_ASSERT(canMatch == shouldMatch);
        });
}

/**
 * **Feature: paper-trading-system, Property 3: 行情驱动撮合正确性**
 * **Validates: Requirements 4.5, 4.6**
 *
 * *对于任意* 虚拟订单簿中的挂单和新到达的行情，
 * 当行情满足成交条件时应触发成交。
 */
TEST_CASE("Property 3: 行情驱动撮合正确性", "[matching_engine][property]") {
    
    rc::prop("行情到达时，满足条件的挂单应被撮合",
        []() {
            // 生成挂单价格
            auto pendingBuyPrice = *rc::gen::inRange(3000, 5000);
            auto pendingSellPrice = *rc::gen::inRange(3000, 5000);
            
            // 生成行情价格
            auto newAskPrice = *rc::gen::inRange(3000, 5000);
            auto newBidPrice = *rc::gen::inRange(3000, 5000);
            
            // 确保行情有效
            RC_PRE(newAskPrice > 0 && newBidPrice > 0);
            
            MarketDataSnapshot snapshot("IF2601");
            snapshot.askPrice1 = static_cast<double>(newAskPrice);
            snapshot.askVolume1 = 100;
            snapshot.bidPrice1 = static_cast<double>(newBidPrice);
            snapshot.bidVolume1 = 100;
            
            // 测试买单
            Order buyOrder;
            buyOrder.clOrdID = "BUY";
            buyOrder.symbol = "IF2601";
            buyOrder.side = OrderSide::BUY;
            buyOrder.ordType = OrderType::LIMIT;
            buyOrder.price = static_cast<double>(pendingBuyPrice);
            buyOrder.orderQty = 1;
            
            MatchingEngine eng;
            bool buyCanMatch = eng.canMatchBuyOrder(buyOrder, snapshot);
            bool buyShouldMatch = (pendingBuyPrice >= newAskPrice);
            RC_ASSERT(buyCanMatch == buyShouldMatch);
            
            // 测试卖单
            Order sellOrder;
            sellOrder.clOrdID = "SELL";
            sellOrder.symbol = "IF2601";
            sellOrder.side = OrderSide::SELL;
            sellOrder.ordType = OrderType::LIMIT;
            sellOrder.price = static_cast<double>(pendingSellPrice);
            sellOrder.orderQty = 1;
            
            bool sellCanMatch = eng.canMatchSellOrder(sellOrder, snapshot);
            bool sellShouldMatch = (pendingSellPrice <= newBidPrice);
            RC_ASSERT(sellCanMatch == sellShouldMatch);
        });
}

/**
 * **Feature: paper-trading-system, Property 4: 市价单撮合正确性**
 * **Validates: Requirements 5.1, 5.2**
 *
 * *对于任意* 市价买单和非空卖盘的行情快照，应立即以卖一价成交；
 * 市价卖单同理。
 */
TEST_CASE("Property 4: 市价单撮合正确性", "[matching_engine][property]") {
    
    rc::prop("市价买单：有卖盘时应可成交",
        []() {
            auto askPrice = *rc::gen::inRange(1000, 10000);
            auto askVolume = *rc::gen::inRange(1, 1000);
            auto bidPrice = *rc::gen::inRange(1000, 10000);
            auto bidVolume = *rc::gen::inRange(0, 1000);  // 买盘可以为空
            
            MarketDataSnapshot snapshot("IF2601");
            snapshot.askPrice1 = static_cast<double>(askPrice);
            snapshot.askVolume1 = askVolume;
            snapshot.bidPrice1 = static_cast<double>(bidPrice);
            snapshot.bidVolume1 = bidVolume;
            
            Order order;
            order.clOrdID = "TEST";
            order.symbol = "IF2601";
            order.side = OrderSide::BUY;
            order.ordType = OrderType::MARKET;
            order.price = 0.0;
            order.orderQty = 1;
            
            MatchingEngine eng;
            bool canMatch = eng.canMatchBuyOrder(order, snapshot);
            bool hasAsk = (askPrice > 0 && askVolume > 0);
            
            RC_ASSERT(canMatch == hasAsk);
        });
    
    rc::prop("市价卖单：有买盘时应可成交",
        []() {
            auto askPrice = *rc::gen::inRange(0, 10000);  // 卖盘可以为空
            auto askVolume = *rc::gen::inRange(0, 1000);
            auto bidPrice = *rc::gen::inRange(1000, 10000);
            auto bidVolume = *rc::gen::inRange(1, 1000);
            
            MarketDataSnapshot snapshot("IF2601");
            snapshot.askPrice1 = static_cast<double>(askPrice);
            snapshot.askVolume1 = askVolume;
            snapshot.bidPrice1 = static_cast<double>(bidPrice);
            snapshot.bidVolume1 = bidVolume;
            
            Order order;
            order.clOrdID = "TEST";
            order.symbol = "IF2601";
            order.side = OrderSide::SELL;
            order.ordType = OrderType::MARKET;
            order.price = 0.0;
            order.orderQty = 1;
            
            MatchingEngine eng;
            bool canMatch = eng.canMatchSellOrder(order, snapshot);
            bool hasBid = (bidPrice > 0 && bidVolume > 0);
            
            RC_ASSERT(canMatch == hasBid);
        });
}

