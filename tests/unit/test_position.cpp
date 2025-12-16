/**
 * @file test_position.cpp
 * @brief Position 结构体单元测试和属性测试
 *
 * 测试持仓数据结构的计算方法和属性。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/model/position.hpp"
#include <cmath>

using namespace fix40;

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("Position 默认构造函数", "[position][unit]") {
    Position pos;
    
    REQUIRE(pos.accountId.empty());
    REQUIRE(pos.instrumentId.empty());
    REQUIRE(pos.longPosition == 0);
    REQUIRE(pos.longAvgPrice == 0.0);
    REQUIRE(pos.longProfit == 0.0);
    REQUIRE(pos.longMargin == 0.0);
    REQUIRE(pos.shortPosition == 0);
    REQUIRE(pos.shortAvgPrice == 0.0);
    REQUIRE(pos.shortProfit == 0.0);
    REQUIRE(pos.shortMargin == 0.0);
}

TEST_CASE("Position 带参数构造函数", "[position][unit]") {
    Position pos("user001", "IF2601");
    
    REQUIRE(pos.accountId == "user001");
    REQUIRE(pos.instrumentId == "IF2601");
    REQUIRE(pos.longPosition == 0);
    REQUIRE(pos.shortPosition == 0);
}

TEST_CASE("Position updateProfit 多头盈亏计算", "[position][unit]") {
    Position pos("user001", "IF2601");
    pos.longPosition = 2;
    pos.longAvgPrice = 4000.0;
    
    SECTION("价格上涨时盈利") {
        pos.updateProfit(4050.0, 300);
        // 盈亏 = (4050 - 4000) * 2 * 300 = 30000
        REQUIRE(pos.longProfit == Approx(30000.0));
    }
    
    SECTION("价格下跌时亏损") {
        pos.updateProfit(3950.0, 300);
        // 盈亏 = (3950 - 4000) * 2 * 300 = -30000
        REQUIRE(pos.longProfit == Approx(-30000.0));
    }
    
    SECTION("价格不变时盈亏为0") {
        pos.updateProfit(4000.0, 300);
        REQUIRE(pos.longProfit == Approx(0.0));
    }
}

TEST_CASE("Position updateProfit 空头盈亏计算", "[position][unit]") {
    Position pos("user001", "IF2601");
    pos.shortPosition = 3;
    pos.shortAvgPrice = 4000.0;
    
    SECTION("价格下跌时盈利") {
        pos.updateProfit(3900.0, 300);
        // 盈亏 = (4000 - 3900) * 3 * 300 = 90000
        REQUIRE(pos.shortProfit == Approx(90000.0));
    }
    
    SECTION("价格上涨时亏损") {
        pos.updateProfit(4100.0, 300);
        // 盈亏 = (4000 - 4100) * 3 * 300 = -90000
        REQUIRE(pos.shortProfit == Approx(-90000.0));
    }
}

TEST_CASE("Position 辅助方法", "[position][unit]") {
    Position pos("user001", "IF2601");
    pos.longPosition = 2;
    pos.longProfit = 10000.0;
    pos.longMargin = 50000.0;
    pos.shortPosition = 1;
    pos.shortProfit = 5000.0;
    pos.shortMargin = 25000.0;
    
    REQUIRE(pos.getTotalProfit() == Approx(15000.0));
    REQUIRE(pos.getTotalPosition() == 3);
    REQUIRE(pos.getTotalMargin() == Approx(75000.0));
    REQUIRE(pos.hasPosition() == true);
    REQUIRE(pos.getNetPosition() == 1);  // 2 - 1 = 1 (净多)
}

TEST_CASE("Position 相等比较", "[position][unit]") {
    Position p1("user001", "IF2601");
    Position p2("user001", "IF2601");
    Position p3("user002", "IF2601");
    
    REQUIRE(p1 == p2);
    REQUIRE(p1 != p3);
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * @brief Position 生成器
 * 
 * 生成有效的 Position 对象用于属性测试。
 */
namespace rc {
template<>
struct Arbitrary<Position> {
    static Gen<Position> arbitrary() {
        return gen::build<Position>(
            gen::set(&Position::accountId, gen::nonEmpty<std::string>()),
            gen::set(&Position::instrumentId, gen::nonEmpty<std::string>()),
            gen::set(&Position::longPosition, gen::inRange<int64_t>(0, 10000)),
            gen::set(&Position::longAvgPrice, gen::positive<double>()),
            gen::set(&Position::longProfit, gen::arbitrary<double>()),
            gen::set(&Position::longMargin, gen::nonNegative<double>()),
            gen::set(&Position::shortPosition, gen::inRange<int64_t>(0, 10000)),
            gen::set(&Position::shortAvgPrice, gen::positive<double>()),
            gen::set(&Position::shortProfit, gen::arbitrary<double>()),
            gen::set(&Position::shortMargin, gen::nonNegative<double>())
        );
    }
};
} // namespace rc

/**
 * **Feature: paper-trading-system, Property 8: 浮动盈亏计算正确性**
 * **Validates: Requirements 7.3**
 * 
 * 对于任意持仓和最新价，浮动盈亏应等于：
 * - 多头：(最新价 - 持仓均价) × 持仓量 × 合约乘数
 * - 空头：(持仓均价 - 最新价) × 持仓量 × 合约乘数
 */
TEST_CASE("Position 属性测试", "[position][property]") {
    
    rc::prop("多头浮动盈亏计算正确",
        []() {
            // 生成有效的持仓参数
            auto longPosition = *rc::gen::inRange<int64_t>(0, 1000);
            auto longAvgPrice = *rc::gen::positive<double>();
            auto lastPrice = *rc::gen::positive<double>();
            auto volumeMultiple = *rc::gen::inRange(1, 1000);
            
            Position pos;
            pos.longPosition = longPosition;
            pos.longAvgPrice = longAvgPrice;
            
            pos.updateProfit(lastPrice, volumeMultiple);
            
            double expected = (lastPrice - longAvgPrice) * longPosition * volumeMultiple;
            
            // 使用相对误差比较浮点数
            if (std::abs(expected) < 1e-10) {
                return std::abs(pos.longProfit) < 1e-10;
            }
            return std::abs(pos.longProfit - expected) / std::abs(expected) < 1e-9;
        });
    
    rc::prop("空头浮动盈亏计算正确",
        []() {
            auto shortPosition = *rc::gen::inRange<int64_t>(0, 1000);
            auto shortAvgPrice = *rc::gen::positive<double>();
            auto lastPrice = *rc::gen::positive<double>();
            auto volumeMultiple = *rc::gen::inRange(1, 1000);
            
            Position pos;
            pos.shortPosition = shortPosition;
            pos.shortAvgPrice = shortAvgPrice;
            
            pos.updateProfit(lastPrice, volumeMultiple);
            
            double expected = (shortAvgPrice - lastPrice) * shortPosition * volumeMultiple;
            
            if (std::abs(expected) < 1e-10) {
                return std::abs(pos.shortProfit) < 1e-10;
            }
            return std::abs(pos.shortProfit - expected) / std::abs(expected) < 1e-9;
        });
    
    rc::prop("总盈亏等于多头盈亏加空头盈亏",
        [](const Position& pos) {
            double expected = pos.longProfit + pos.shortProfit;
            return pos.getTotalProfit() == expected;
        });
    
    rc::prop("总持仓等于多头持仓加空头持仓",
        [](const Position& pos) {
            int64_t expected = pos.longPosition + pos.shortPosition;
            return pos.getTotalPosition() == expected;
        });
    
    rc::prop("净持仓等于多头减空头",
        [](const Position& pos) {
            int64_t expected = pos.longPosition - pos.shortPosition;
            return pos.getNetPosition() == expected;
        });
    
    rc::prop("hasPosition 正确判断是否有持仓",
        [](const Position& pos) {
            bool expected = (pos.longPosition > 0 || pos.shortPosition > 0);
            return pos.hasPosition() == expected;
        });
    
    rc::prop("Position 相等比较自反性",
        [](const Position& pos) {
            return pos == pos;
        });
}
