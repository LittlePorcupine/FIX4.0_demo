/**
 * @file test_instrument.cpp
 * @brief Instrument 结构体单元测试和属性测试
 *
 * 测试合约信息数据结构的计算方法和属性。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/instrument.hpp"
#include <cmath>

using namespace fix40;

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("Instrument 默认构造函数", "[instrument][unit]") {
    Instrument inst;
    
    REQUIRE(inst.instrumentId.empty());
    REQUIRE(inst.exchangeId.empty());
    REQUIRE(inst.productId.empty());
    REQUIRE(inst.priceTick == 0.0);
    REQUIRE(inst.volumeMultiple == 0);
    REQUIRE(inst.marginRate == 0.0);
    REQUIRE(inst.upperLimitPrice == 0.0);
    REQUIRE(inst.lowerLimitPrice == 0.0);
    REQUIRE(inst.preSettlementPrice == 0.0);
}

TEST_CASE("Instrument 带参数构造函数", "[instrument][unit]") {
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    
    REQUIRE(inst.instrumentId == "IF2601");
    REQUIRE(inst.exchangeId == "CFFEX");
    REQUIRE(inst.productId == "IF");
    REQUIRE(inst.priceTick == 0.2);
    REQUIRE(inst.volumeMultiple == 300);
    REQUIRE(inst.marginRate == 0.12);
}

TEST_CASE("Instrument calculateMargin 保证金计算", "[instrument][unit]") {
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    
    SECTION("标准计算") {
        // 保证金 = 4000 * 2 * 300 * 0.12 = 288000
        double margin = inst.calculateMargin(4000.0, 2);
        REQUIRE(margin == Approx(288000.0));
    }
    
    SECTION("单手计算") {
        // 保证金 = 4000 * 1 * 300 * 0.12 = 144000
        double margin = inst.calculateMargin(4000.0, 1);
        REQUIRE(margin == Approx(144000.0));
    }
    
    SECTION("零数量") {
        double margin = inst.calculateMargin(4000.0, 0);
        REQUIRE(margin == 0.0);
    }
}

TEST_CASE("Instrument isPriceValid 价格有效性检查", "[instrument][unit]") {
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    inst.lowerLimitPrice = 3800.0;
    inst.upperLimitPrice = 4200.0;
    
    SECTION("价格在范围内") {
        REQUIRE(inst.isPriceValid(4000.0) == true);
        REQUIRE(inst.isPriceValid(3800.0) == true);  // 边界
        REQUIRE(inst.isPriceValid(4200.0) == true);  // 边界
    }
    
    SECTION("价格超出范围") {
        REQUIRE(inst.isPriceValid(3799.0) == false);
        REQUIRE(inst.isPriceValid(4201.0) == false);
    }
    
    SECTION("涨跌停未设置时") {
        Instrument inst2;
        REQUIRE(inst2.isPriceValid(9999.0) == true);
    }
}

TEST_CASE("Instrument isPriceTickValid 最小变动价位检查", "[instrument][unit]") {
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    
    SECTION("价格符合最小变动价位") {
        REQUIRE(inst.isPriceTickValid(4000.0) == true);
        REQUIRE(inst.isPriceTickValid(4000.2) == true);
        REQUIRE(inst.isPriceTickValid(4000.4) == true);
    }
    
    SECTION("价格不符合最小变动价位") {
        REQUIRE(inst.isPriceTickValid(4000.1) == false);
        REQUIRE(inst.isPriceTickValid(4000.15) == false);
    }
    
    SECTION("最小变动价位未设置时") {
        Instrument inst2;
        REQUIRE(inst2.isPriceTickValid(4000.123) == true);
    }
}

TEST_CASE("Instrument updateLimitPrices 更新涨跌停", "[instrument][unit]") {
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    
    inst.updateLimitPrices(4200.0, 3800.0);
    
    REQUIRE(inst.upperLimitPrice == 4200.0);
    REQUIRE(inst.lowerLimitPrice == 3800.0);
}

TEST_CASE("Instrument 相等比较", "[instrument][unit]") {
    Instrument i1("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    Instrument i2("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    Instrument i3("IF2602", "CFFEX", "IF", 0.2, 300, 0.12);
    
    REQUIRE(i1 == i2);
    REQUIRE(i1 != i3);
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * @brief Instrument 生成器
 * 
 * 生成有效的 Instrument 对象用于属性测试。
 */
namespace rc {
template<>
struct Arbitrary<Instrument> {
    static Gen<Instrument> arbitrary() {
        return gen::build<Instrument>(
            gen::set(&Instrument::instrumentId, gen::nonEmpty<std::string>()),
            gen::set(&Instrument::exchangeId, gen::nonEmpty<std::string>()),
            gen::set(&Instrument::productId, gen::nonEmpty<std::string>()),
            gen::set(&Instrument::priceTick, gen::positive<double>()),
            gen::set(&Instrument::volumeMultiple, gen::inRange(1, 1000)),
            gen::set(&Instrument::marginRate, gen::map(
                gen::inRange(1, 100),
                [](int r) { return r / 100.0; }  // 1% - 100%
            )),
            gen::set(&Instrument::upperLimitPrice, gen::positive<double>()),
            gen::set(&Instrument::lowerLimitPrice, gen::positive<double>()),
            gen::set(&Instrument::preSettlementPrice, gen::positive<double>())
        );
    }
};
} // namespace rc

/**
 * **Feature: paper-trading-system, Property 5: 保证金计算正确性**
 * **Validates: Requirements 8.1**
 * 
 * 对于任意订单、合约信息，计算的保证金应等于：
 * 价格 × 数量 × 合约乘数 × 保证金率
 */
TEST_CASE("Instrument 属性测试", "[instrument][property]") {
    
    rc::prop("保证金计算正确性",
        []() {
            // 生成有效的合约参数
            auto price = *rc::gen::positive<double>();
            auto volume = *rc::gen::inRange<int64_t>(1, 1000);
            auto volumeMultiple = *rc::gen::inRange(1, 1000);
            auto marginRate = *rc::gen::map(
                rc::gen::inRange(1, 100),
                [](int r) { return r / 100.0; }
            );
            
            Instrument inst;
            inst.volumeMultiple = volumeMultiple;
            inst.marginRate = marginRate;
            
            double actual = inst.calculateMargin(price, volume);
            double expected = price * volume * volumeMultiple * marginRate;
            
            // 使用相对误差比较浮点数
            if (std::abs(expected) < 1e-10) {
                return std::abs(actual) < 1e-10;
            }
            return std::abs(actual - expected) / std::abs(expected) < 1e-9;
        });
    
    rc::prop("价格在涨跌停范围内时有效",
        []() {
            auto lower = *rc::gen::positive<double>();
            auto spread = *rc::gen::positive<double>();
            double upper = lower + spread;  // 确保 upper > lower
            // 生成 0-100 的整数，然后转换为 0.0-1.0 的比例
            auto ratio = *rc::gen::inRange(0, 100);
            double price = lower + (ratio / 100.0) * spread;
            
            Instrument inst;
            inst.lowerLimitPrice = lower;
            inst.upperLimitPrice = upper;
            
            return inst.isPriceValid(price);
        });
    
    rc::prop("价格低于跌停时无效",
        []() {
            // 使用整数生成器，然后转换为 double
            auto lowerInt = *rc::gen::inRange(100, 10000);
            auto spreadInt = *rc::gen::inRange(100, 1000);
            auto belowInt = *rc::gen::inRange(1, 100);
            
            double lower = static_cast<double>(lowerInt);
            double upper = lower + static_cast<double>(spreadInt);
            double price = lower - static_cast<double>(belowInt) * 0.01;
            
            Instrument inst;
            inst.lowerLimitPrice = lower;
            inst.upperLimitPrice = upper;
            
            return !inst.isPriceValid(price);
        });
    
    rc::prop("价格高于涨停时无效",
        []() {
            auto lowerInt = *rc::gen::inRange(100, 10000);
            auto spreadInt = *rc::gen::inRange(100, 1000);
            auto aboveInt = *rc::gen::inRange(1, 100);
            
            double lower = static_cast<double>(lowerInt);
            double upper = lower + static_cast<double>(spreadInt);
            double price = upper + static_cast<double>(aboveInt) * 0.01;
            
            Instrument inst;
            inst.lowerLimitPrice = lower;
            inst.upperLimitPrice = upper;
            
            return !inst.isPriceValid(price);
        });
    
    rc::prop("Instrument 相等比较自反性",
        [](const Instrument& inst) {
            return inst == inst;
        });
}
