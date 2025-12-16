/**
 * @file test_position_manager.cpp
 * @brief PositionManager 单元测试和属性测试
 *
 * 测试持仓管理器的开仓、平仓、盈亏计算功能。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/position_manager.hpp"
#include <cmath>

using namespace fix40;

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("PositionManager 默认构造", "[position_manager][unit]") {
    PositionManager mgr;
    
    REQUIRE(mgr.size() == 0);
    REQUIRE(mgr.getAllPositions().empty());
}

TEST_CASE("PositionManager openPosition 多头开仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos.has_value());
    REQUIRE(pos->longPosition == 2);
    REQUIRE(pos->longAvgPrice == 4000.0);
    REQUIRE(pos->longMargin == 240000.0);
    REQUIRE(pos->shortPosition == 0);
}

TEST_CASE("PositionManager openPosition 空头开仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::SELL, 3, 4100.0, 369000.0);
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos.has_value());
    REQUIRE(pos->shortPosition == 3);
    REQUIRE(pos->shortAvgPrice == 4100.0);
    REQUIRE(pos->shortMargin == 369000.0);
    REQUIRE(pos->longPosition == 0);
}

TEST_CASE("PositionManager openPosition 加仓计算均价", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 第一次开仓：2手 @ 4000
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    // 第二次开仓：3手 @ 4100
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 3, 4100.0, 369000.0);
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos.has_value());
    REQUIRE(pos->longPosition == 5);
    // 均价 = (4000*2 + 4100*3) / 5 = (8000 + 12300) / 5 = 4060
    REQUIRE(pos->longAvgPrice == Approx(4060.0));
    REQUIRE(pos->longMargin == Approx(609000.0));
}

TEST_CASE("PositionManager closePosition 平多头", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 开仓：2手 @ 4000
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    // 平仓：1手 @ 4100，合约乘数300
    // 盈亏 = (4100 - 4000) * 1 * 300 = 30000
    double profit = mgr.closePosition("user001", "IF2601", OrderSide::SELL, 1, 4100.0, 300);
    
    REQUIRE(profit == Approx(30000.0));
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos->longPosition == 1);
}

TEST_CASE("PositionManager closePosition 部分平仓保证金按比例减少", "[position_manager][unit]") {
    PositionManager mgr;
    
    SECTION("多头部分平仓") {
        // 开仓：4手 @ 4000，保证金 480000
        mgr.openPosition("user001", "IF2601", OrderSide::BUY, 4, 4000.0, 480000.0);
        
        // 平仓：1手（25%）
        mgr.closePosition("user001", "IF2601", OrderSide::SELL, 1, 4100.0, 300);
        
        auto pos = mgr.getPosition("user001", "IF2601");
        REQUIRE(pos->longPosition == 3);
        // 保证金应减少25%：480000 * 0.75 = 360000
        REQUIRE(pos->longMargin == Approx(360000.0));
    }
    
    SECTION("空头部分平仓") {
        // 开仓：4手 @ 4100，保证金 492000
        mgr.openPosition("user001", "IF2601", OrderSide::SELL, 4, 4100.0, 492000.0);
        
        // 平仓：2手（50%）
        mgr.closePosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 300);
        
        auto pos = mgr.getPosition("user001", "IF2601");
        REQUIRE(pos->shortPosition == 2);
        // 保证金应减少50%：492000 * 0.5 = 246000
        REQUIRE(pos->shortMargin == Approx(246000.0));
    }
    
    SECTION("多次部分平仓") {
        // 开仓：10手，保证金 1000000
        mgr.openPosition("user001", "IF2601", OrderSide::BUY, 10, 4000.0, 1000000.0);
        
        // 第一次平仓：3手（30%）
        mgr.closePosition("user001", "IF2601", OrderSide::SELL, 3, 4050.0, 300);
        
        auto pos1 = mgr.getPosition("user001", "IF2601");
        REQUIRE(pos1->longPosition == 7);
        // 保证金：1000000 * 0.7 = 700000
        REQUIRE(pos1->longMargin == Approx(700000.0));
        
        // 第二次平仓：2手（剩余7手中的2手，约28.57%）
        mgr.closePosition("user001", "IF2601", OrderSide::SELL, 2, 4060.0, 300);
        
        auto pos2 = mgr.getPosition("user001", "IF2601");
        REQUIRE(pos2->longPosition == 5);
        // 保证金：700000 * (5/7) = 500000
        REQUIRE(pos2->longMargin == Approx(500000.0));
    }
}

TEST_CASE("PositionManager closePosition 平空头", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 开仓：2手 @ 4100
    mgr.openPosition("user001", "IF2601", OrderSide::SELL, 2, 4100.0, 246000.0);
    
    // 平仓：1手 @ 4000，合约乘数300
    // 盈亏 = (4100 - 4000) * 1 * 300 = 30000
    double profit = mgr.closePosition("user001", "IF2601", OrderSide::BUY, 1, 4000.0, 300);
    
    REQUIRE(profit == Approx(30000.0));
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos->shortPosition == 1);
}

TEST_CASE("PositionManager closePosition 全部平仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    double profit = mgr.closePosition("user001", "IF2601", OrderSide::SELL, 2, 4050.0, 300);
    
    // 盈亏 = (4050 - 4000) * 2 * 300 = 30000
    REQUIRE(profit == Approx(30000.0));
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos->longPosition == 0);
    REQUIRE(pos->longAvgPrice == 0.0);
}

TEST_CASE("PositionManager updateProfit 更新浮动盈亏", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 开仓：2手多头 @ 4000
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    // 更新盈亏，最新价4050，合约乘数300
    double profit = mgr.updateProfit("user001", "IF2601", 4050.0, 300);
    
    // 浮动盈亏 = (4050 - 4000) * 2 * 300 = 30000
    REQUIRE(profit == Approx(30000.0));
    
    auto pos = mgr.getPosition("user001", "IF2601");
    REQUIRE(pos->longProfit == Approx(30000.0));
}

TEST_CASE("PositionManager updateAllProfits 批量更新盈亏", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 两个账户持有同一合约
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    mgr.openPosition("user002", "IF2601", OrderSide::SELL, 1, 4100.0, 123000.0);
    
    MarketDataSnapshot snapshot;
    snapshot.instrumentId = "IF2601";
    snapshot.lastPrice = 4050.0;
    
    mgr.updateAllProfits(snapshot, 300);
    
    auto pos1 = mgr.getPosition("user001", "IF2601");
    // 多头盈亏 = (4050 - 4000) * 2 * 300 = 30000
    REQUIRE(pos1->longProfit == Approx(30000.0));
    
    auto pos2 = mgr.getPosition("user002", "IF2601");
    // 空头盈亏 = (4100 - 4050) * 1 * 300 = 15000
    REQUIRE(pos2->shortProfit == Approx(15000.0));
}

TEST_CASE("PositionManager getTotalProfit 获取账户总盈亏", "[position_manager][unit]") {
    PositionManager mgr;
    
    // 同一账户持有两个合约
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    mgr.openPosition("user001", "IC2601", OrderSide::SELL, 1, 6000.0, 120000.0);
    
    // 更新IF2601盈亏
    mgr.updateProfit("user001", "IF2601", 4050.0, 300);  // +30000
    // 更新IC2601盈亏
    mgr.updateProfit("user001", "IC2601", 5900.0, 200);  // +20000
    
    double total = mgr.getTotalProfit("user001");
    REQUIRE(total == Approx(50000.0));
}

TEST_CASE("PositionManager getPositionsByAccount 获取账户所有持仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    mgr.openPosition("user001", "IC2601", OrderSide::SELL, 1, 6000.0, 120000.0);
    mgr.openPosition("user002", "IF2601", OrderSide::BUY, 1, 4000.0, 120000.0);
    
    auto positions = mgr.getPositionsByAccount("user001");
    REQUIRE(positions.size() == 2);
}

TEST_CASE("PositionManager hasPosition 检查持仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    
    REQUIRE(mgr.hasPosition("user001", "IF2601") == true);
    REQUIRE(mgr.hasPosition("user001", "IC2601") == false);
    REQUIRE(mgr.hasPosition("user002", "IF2601") == false);
}

TEST_CASE("PositionManager clear 清空持仓", "[position_manager][unit]") {
    PositionManager mgr;
    
    mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
    mgr.openPosition("user002", "IC2601", OrderSide::SELL, 1, 6000.0, 120000.0);
    
    REQUIRE(mgr.size() == 2);
    
    mgr.clear();
    
    REQUIRE(mgr.size() == 0);
}

TEST_CASE("PositionManager 亏损场景", "[position_manager][unit]") {
    PositionManager mgr;
    
    SECTION("多头亏损") {
        mgr.openPosition("user001", "IF2601", OrderSide::BUY, 2, 4000.0, 240000.0);
        
        // 平仓价低于开仓价
        double profit = mgr.closePosition("user001", "IF2601", OrderSide::SELL, 2, 3900.0, 300);
        
        // 盈亏 = (3900 - 4000) * 2 * 300 = -60000
        REQUIRE(profit == Approx(-60000.0));
    }
    
    SECTION("空头亏损") {
        mgr.openPosition("user001", "IF2601", OrderSide::SELL, 2, 4000.0, 240000.0);
        
        // 平仓价高于开仓价
        double profit = mgr.closePosition("user001", "IF2601", OrderSide::BUY, 2, 4100.0, 300);
        
        // 盈亏 = (4000 - 4100) * 2 * 300 = -60000
        REQUIRE(profit == Approx(-60000.0));
    }
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 7: 持仓计算正确性**
 * **Validates: Requirements 7.1**
 *
 * 对于任意开仓成交序列，持仓数量应等于成交数量之和，
 * 持仓均价应等于加权平均价。
 */
TEST_CASE("PositionManager 属性测试 - 持仓计算", "[position_manager][property]") {
    
    rc::prop("多次开仓后持仓量等于总开仓量",
        []() {
            PositionManager mgr;
            
            auto numTrades = *rc::gen::inRange(1, 10);
            int64_t totalVolume = 0;
            
            for (int i = 0; i < numTrades; ++i) {
                auto volume = *rc::gen::inRange<int64_t>(1, 100);
                auto price = *rc::gen::inRange(3000, 5000);
                auto margin = *rc::gen::inRange(10000, 500000);
                
                mgr.openPosition("test", "IF2601", OrderSide::BUY, 
                                 volume, static_cast<double>(price), static_cast<double>(margin));
                totalVolume += volume;
            }
            
            auto pos = mgr.getPosition("test", "IF2601");
            RC_ASSERT(pos.has_value());
            RC_ASSERT(pos->longPosition == totalVolume);
        });
    
    rc::prop("开仓均价等于加权平均价",
        []() {
            PositionManager mgr;
            
            auto numTrades = *rc::gen::inRange(1, 5);
            double totalCost = 0.0;
            int64_t totalVolume = 0;
            
            for (int i = 0; i < numTrades; ++i) {
                auto volume = *rc::gen::inRange<int64_t>(1, 50);
                auto price = *rc::gen::inRange(3000, 5000);
                auto margin = *rc::gen::inRange(10000, 500000);
                
                mgr.openPosition("test", "IF2601", OrderSide::BUY,
                                 volume, static_cast<double>(price), static_cast<double>(margin));
                
                totalCost += static_cast<double>(price) * volume;
                totalVolume += volume;
            }
            
            auto pos = mgr.getPosition("test", "IF2601");
            RC_ASSERT(pos.has_value());
            
            double expectedAvg = totalCost / totalVolume;
            RC_ASSERT(std::abs(pos->longAvgPrice - expectedAvg) < 0.01);
        });
}

/**
 * **Feature: paper-trading-system, Property 9: 平仓盈亏计算正确性**
 * **Validates: Requirements 7.2**
 *
 * 对于任意平仓成交，平仓盈亏应等于：
 * (平仓价 - 持仓均价) × 平仓量 × 合约乘数（多头平仓），空头取反
 */
TEST_CASE("PositionManager 属性测试 - 平仓盈亏", "[position_manager][property]") {
    
    rc::prop("多头平仓盈亏计算正确",
        []() {
            PositionManager mgr;
            
            auto openPrice = *rc::gen::inRange(3000, 5000);
            auto closePrice = *rc::gen::inRange(3000, 5000);
            auto volume = *rc::gen::inRange<int64_t>(1, 100);
            auto volumeMultiple = *rc::gen::inRange(100, 500);
            
            // 开仓
            mgr.openPosition("test", "IF2601", OrderSide::BUY,
                             volume, static_cast<double>(openPrice), 100000.0);
            
            // 平仓
            double actualProfit = mgr.closePosition("test", "IF2601", OrderSide::SELL,
                                                     volume, static_cast<double>(closePrice), volumeMultiple);
            
            // 预期盈亏
            double expectedProfit = (closePrice - openPrice) * volume * volumeMultiple;
            
            RC_ASSERT(std::abs(actualProfit - expectedProfit) < 0.01);
        });
    
    rc::prop("空头平仓盈亏计算正确",
        []() {
            PositionManager mgr;
            
            auto openPrice = *rc::gen::inRange(3000, 5000);
            auto closePrice = *rc::gen::inRange(3000, 5000);
            auto volume = *rc::gen::inRange<int64_t>(1, 100);
            auto volumeMultiple = *rc::gen::inRange(100, 500);
            
            // 开仓
            mgr.openPosition("test", "IF2601", OrderSide::SELL,
                             volume, static_cast<double>(openPrice), 100000.0);
            
            // 平仓
            double actualProfit = mgr.closePosition("test", "IF2601", OrderSide::BUY,
                                                     volume, static_cast<double>(closePrice), volumeMultiple);
            
            // 预期盈亏（空头：持仓均价 - 平仓价）
            double expectedProfit = (openPrice - closePrice) * volume * volumeMultiple;
            
            RC_ASSERT(std::abs(actualProfit - expectedProfit) < 0.01);
        });
    
    rc::prop("浮动盈亏计算正确",
        []() {
            PositionManager mgr;
            
            auto openPrice = *rc::gen::inRange(3000, 5000);
            auto lastPrice = *rc::gen::inRange(3000, 5000);
            auto volume = *rc::gen::inRange<int64_t>(1, 100);
            auto volumeMultiple = *rc::gen::inRange(100, 500);
            
            // 开仓
            mgr.openPosition("test", "IF2601", OrderSide::BUY,
                             volume, static_cast<double>(openPrice), 100000.0);
            
            // 更新盈亏
            double actualProfit = mgr.updateProfit("test", "IF2601",
                                                    static_cast<double>(lastPrice), volumeMultiple);
            
            // 预期浮动盈亏
            double expectedProfit = (lastPrice - openPrice) * volume * volumeMultiple;
            
            RC_ASSERT(std::abs(actualProfit - expectedProfit) < 0.01);
        });
}
