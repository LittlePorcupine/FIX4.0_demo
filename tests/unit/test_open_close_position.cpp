/**
 * @file test_open_close_position.cpp
 * @brief 开平仓逻辑单元测试
 *
 * 测试期货交易中的开平仓逻辑：
 * - 买入平空（有空仓时买入优先平空）
 * - 卖出平多（有多仓时卖出优先平多）
 * - 部分平仓+部分开仓
 */

#include "../catch2/catch.hpp"
#include "app/manager/position_manager.hpp"
#include <cmath>
#include <algorithm>

using namespace fix40;

// 模拟 SimulationApp::handleFill 的开平仓逻辑
struct FillResult {
    int64_t closeQty = 0;
    int64_t openQty = 0;
    double closeProfit = 0.0;
};

FillResult simulateHandleFill(
    PositionManager& posMgr,
    const std::string& accountId,
    const std::string& symbol,
    OrderSide side,
    int64_t fillQty,
    double fillPrice,
    int volumeMultiple,
    double marginRate) {
    
    FillResult result;
    
    // 获取当前持仓
    Position position;
    auto posOpt = posMgr.getPosition(accountId, symbol);
    if (posOpt) {
        position = *posOpt;
    }
    
    // 计算可平仓数量和开仓数量
    result.openQty = fillQty;
    
    if (side == OrderSide::BUY && position.shortPosition > 0) {
        // 买单：优先平空仓
        result.closeQty = std::min(fillQty, position.shortPosition);
        result.openQty = fillQty - result.closeQty;
    } else if (side == OrderSide::SELL && position.longPosition > 0) {
        // 卖单：优先平多仓
        result.closeQty = std::min(fillQty, position.longPosition);
        result.openQty = fillQty - result.closeQty;
    }
    
    // 处理平仓
    if (result.closeQty > 0) {
        result.closeProfit = posMgr.closePosition(
            accountId, symbol, side,
            result.closeQty, fillPrice, volumeMultiple);
    }
    
    // 处理开仓
    if (result.openQty > 0) {
        double margin = fillPrice * result.openQty * volumeMultiple * marginRate;
        posMgr.openPosition(accountId, symbol, side, result.openQty, fillPrice, margin);
    }
    
    return result;
}

// =============================================================================
// 买入平空测试
// =============================================================================

TEST_CASE("开平仓逻辑 - 买入平空", "[open_close][unit]") {
    PositionManager posMgr;
    const std::string accountId = "user001";
    const std::string symbol = "IF2601";
    const int volumeMultiple = 300;
    const double marginRate = 0.1;
    
    SECTION("买入完全平空") {
        // 先开空仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::SELL, 2, 4000.0, 240000.0);
        
        auto posBefore = posMgr.getPosition(accountId, symbol);
        REQUIRE(posBefore->shortPosition == 2);
        REQUIRE(posBefore->longPosition == 0);
        
        // 买入 2 手 @ 3900（平空，盈利）
        auto result = simulateHandleFill(posMgr, accountId, symbol, 
            OrderSide::BUY, 2, 3900.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 0);
        // 盈亏 = (4000 - 3900) * 2 * 300 = 60000
        REQUIRE(result.closeProfit == Approx(60000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->shortPosition == 0);
        REQUIRE(posAfter->longPosition == 0);
    }
    
    SECTION("买入部分平空") {
        // 先开空仓 3 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::SELL, 3, 4000.0, 360000.0);
        
        // 买入 2 手 @ 3950（平空 2 手）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::BUY, 2, 3950.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 0);
        // 盈亏 = (4000 - 3950) * 2 * 300 = 30000
        REQUIRE(result.closeProfit == Approx(30000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->shortPosition == 1);
        REQUIRE(posAfter->longPosition == 0);
    }
    
    SECTION("买入平空后反手开多") {
        // 先开空仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::SELL, 2, 4000.0, 240000.0);
        
        // 买入 5 手 @ 3900（平空 2 手 + 开多 3 手）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::BUY, 5, 3900.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 3);
        // 平空盈亏 = (4000 - 3900) * 2 * 300 = 60000
        REQUIRE(result.closeProfit == Approx(60000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->shortPosition == 0);
        REQUIRE(posAfter->longPosition == 3);
        REQUIRE(posAfter->longAvgPrice == Approx(3900.0));
    }
}

// =============================================================================
// 卖出平多测试
// =============================================================================

TEST_CASE("开平仓逻辑 - 卖出平多", "[open_close][unit]") {
    PositionManager posMgr;
    const std::string accountId = "user001";
    const std::string symbol = "IF2601";
    const int volumeMultiple = 300;
    const double marginRate = 0.1;
    
    SECTION("卖出完全平多") {
        // 先开多仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::BUY, 2, 4000.0, 240000.0);
        
        // 卖出 2 手 @ 4100（平多，盈利）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 2, 4100.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 0);
        // 盈亏 = (4100 - 4000) * 2 * 300 = 60000
        REQUIRE(result.closeProfit == Approx(60000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->longPosition == 0);
        REQUIRE(posAfter->shortPosition == 0);
    }
    
    SECTION("卖出部分平多") {
        // 先开多仓 3 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::BUY, 3, 4000.0, 360000.0);
        
        // 卖出 2 手 @ 4050（平多 2 手）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 2, 4050.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 0);
        // 盈亏 = (4050 - 4000) * 2 * 300 = 30000
        REQUIRE(result.closeProfit == Approx(30000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->longPosition == 1);
        REQUIRE(posAfter->shortPosition == 0);
    }
    
    SECTION("卖出平多后反手开空") {
        // 先开多仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::BUY, 2, 4000.0, 240000.0);
        
        // 卖出 5 手 @ 4100（平多 2 手 + 开空 3 手）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 5, 4100.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 2);
        REQUIRE(result.openQty == 3);
        // 平多盈亏 = (4100 - 4000) * 2 * 300 = 60000
        REQUIRE(result.closeProfit == Approx(60000.0));
        
        auto posAfter = posMgr.getPosition(accountId, symbol);
        REQUIRE(posAfter->longPosition == 0);
        REQUIRE(posAfter->shortPosition == 3);
        REQUIRE(posAfter->shortAvgPrice == Approx(4100.0));
    }
}

// =============================================================================
// 无持仓时直接开仓
// =============================================================================

TEST_CASE("开平仓逻辑 - 无持仓直接开仓", "[open_close][unit]") {
    PositionManager posMgr;
    const std::string accountId = "user001";
    const std::string symbol = "IF2601";
    const int volumeMultiple = 300;
    const double marginRate = 0.1;
    
    SECTION("无持仓买入开多") {
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::BUY, 3, 4000.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 0);
        REQUIRE(result.openQty == 3);
        REQUIRE(result.closeProfit == 0.0);
        
        auto pos = posMgr.getPosition(accountId, symbol);
        REQUIRE(pos->longPosition == 3);
        REQUIRE(pos->shortPosition == 0);
    }
    
    SECTION("无持仓卖出开空") {
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 3, 4000.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 0);
        REQUIRE(result.openQty == 3);
        REQUIRE(result.closeProfit == 0.0);
        
        auto pos = posMgr.getPosition(accountId, symbol);
        REQUIRE(pos->longPosition == 0);
        REQUIRE(pos->shortPosition == 3);
    }
}

// =============================================================================
// 同向加仓（不触发平仓）
// =============================================================================

TEST_CASE("开平仓逻辑 - 同向加仓", "[open_close][unit]") {
    PositionManager posMgr;
    const std::string accountId = "user001";
    const std::string symbol = "IF2601";
    const int volumeMultiple = 300;
    const double marginRate = 0.1;
    
    SECTION("有多仓时继续买入加仓") {
        // 先开多仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::BUY, 2, 4000.0, 240000.0);
        
        // 继续买入 3 手 @ 4100（加仓，不平仓）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::BUY, 3, 4100.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 0);
        REQUIRE(result.openQty == 3);
        REQUIRE(result.closeProfit == 0.0);
        
        auto pos = posMgr.getPosition(accountId, symbol);
        REQUIRE(pos->longPosition == 5);
        // 均价 = (4000*2 + 4100*3) / 5 = 4060
        REQUIRE(pos->longAvgPrice == Approx(4060.0));
    }
    
    SECTION("有空仓时继续卖出加仓") {
        // 先开空仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::SELL, 2, 4000.0, 240000.0);
        
        // 继续卖出 3 手 @ 3900（加仓，不平仓）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 3, 3900.0, volumeMultiple, marginRate);
        
        REQUIRE(result.closeQty == 0);
        REQUIRE(result.openQty == 3);
        REQUIRE(result.closeProfit == 0.0);
        
        auto pos = posMgr.getPosition(accountId, symbol);
        REQUIRE(pos->shortPosition == 5);
        // 均价 = (4000*2 + 3900*3) / 5 = 3940
        REQUIRE(pos->shortAvgPrice == Approx(3940.0));
    }
}

// =============================================================================
// 亏损场景
// =============================================================================

TEST_CASE("开平仓逻辑 - 亏损平仓", "[open_close][unit]") {
    PositionManager posMgr;
    const std::string accountId = "user001";
    const std::string symbol = "IF2601";
    const int volumeMultiple = 300;
    const double marginRate = 0.1;
    
    SECTION("买入平空亏损") {
        // 开空仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::SELL, 2, 4000.0, 240000.0);
        
        // 买入平空 @ 4100（亏损）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::BUY, 2, 4100.0, volumeMultiple, marginRate);
        
        // 亏损 = (4000 - 4100) * 2 * 300 = -60000
        REQUIRE(result.closeProfit == Approx(-60000.0));
    }
    
    SECTION("卖出平多亏损") {
        // 开多仓 2 手 @ 4000
        posMgr.openPosition(accountId, symbol, OrderSide::BUY, 2, 4000.0, 240000.0);
        
        // 卖出平多 @ 3900（亏损）
        auto result = simulateHandleFill(posMgr, accountId, symbol,
            OrderSide::SELL, 2, 3900.0, volumeMultiple, marginRate);
        
        // 亏损 = (3900 - 4000) * 2 * 300 = -60000
        REQUIRE(result.closeProfit == Approx(-60000.0));
    }
}
