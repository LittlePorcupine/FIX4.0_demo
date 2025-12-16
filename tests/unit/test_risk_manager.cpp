/**
 * @file test_risk_manager.cpp
 * @brief RiskManager 单元测试和属性测试
 *
 * 测试风控管理器的资金检查、价格检查、持仓检查功能。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/risk_manager.hpp"
#include <cmath>

using namespace fix40;

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * @brief 创建测试用的订单
 */
Order createTestOrder(
    const std::string& symbol,
    OrderSide side,
    OrderType ordType,
    double price,
    int64_t qty
) {
    Order order;
    order.symbol = symbol;
    order.side = side;
    order.ordType = ordType;
    order.price = price;
    order.orderQty = qty;
    return order;
}

/**
 * @brief 创建测试用的合约
 */
Instrument createTestInstrument(
    const std::string& id = "IF2601",
    double priceTick = 0.2,
    int volumeMultiple = 300,
    double marginRate = 0.12,
    double upperLimit = 4200.0,
    double lowerLimit = 3800.0
) {
    Instrument inst(id, "CFFEX", "IF", priceTick, volumeMultiple, marginRate);
    inst.updateLimitPrices(upperLimit, lowerLimit);
    return inst;
}

/**
 * @brief 创建测试用的行情快照
 */
MarketDataSnapshot createTestSnapshot(
    const std::string& id = "IF2601",
    double bidPrice = 4000.0,
    int32_t bidVolume = 100,
    double askPrice = 4000.2,
    int32_t askVolume = 50
) {
    MarketDataSnapshot snapshot(id);
    snapshot.bidPrice1 = bidPrice;
    snapshot.bidVolume1 = bidVolume;
    snapshot.askPrice1 = askPrice;
    snapshot.askVolume1 = askVolume;
    snapshot.lastPrice = (bidPrice + askPrice) / 2.0;
    return snapshot;
}

// =============================================================================
// 单元测试 - checkMargin 资金检查
// =============================================================================

TEST_CASE("RiskManager checkMargin 资金检查", "[risk_manager][unit]") {
    RiskManager riskMgr;
    Instrument inst = createTestInstrument();
    
    SECTION("资金充足时通过") {
        Account account("user001", 1000000.0);
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 2);
        
        // 所需保证金 = 4000 * 2 * 300 * 0.12 = 288000
        CheckResult result = riskMgr.checkMargin(order, account, inst);
        
        REQUIRE(result.passed == true);
        REQUIRE(result.rejectReason == RejectReason::NONE);
    }
    
    SECTION("资金不足时拒绝") {
        Account account("user001", 100000.0);  // 只有10万
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 2);
        
        // 所需保证金 = 288000 > 100000
        CheckResult result = riskMgr.checkMargin(order, account, inst);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_FUNDS);
        REQUIRE(result.rejectText.find("Insufficient funds") != std::string::npos);
    }
    
    SECTION("资金刚好足够时通过") {
        // 所需保证金 = 4000 * 1 * 300 * 0.12 = 144000
        Account account("user001", 144000.0);
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 1);
        
        CheckResult result = riskMgr.checkMargin(order, account, inst);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("市价买单使用涨停价计算保证金") {
        // 涨停价 4200，所需保证金 = 4200 * 1 * 300 * 0.12 = 151200
        Account account("user001", 150000.0);  // 不够
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkMargin(order, account, inst);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_FUNDS);
    }
    
    SECTION("市价卖单使用跌停价计算保证金") {
        // 跌停价 3800，所需保证金 = 3800 * 1 * 300 * 0.12 = 136800
        Account account("user001", 140000.0);  // 足够
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkMargin(order, account, inst);
        
        REQUIRE(result.passed == true);
    }
}

// =============================================================================
// 单元测试 - checkPrice 价格检查
// =============================================================================

TEST_CASE("RiskManager checkPrice 价格检查", "[risk_manager][unit]") {
    RiskManager riskMgr;
    Instrument inst = createTestInstrument();  // 涨停4200，跌停3800
    
    SECTION("价格在范围内通过") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("价格等于涨停价通过") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4200.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("价格等于跌停价通过") {
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 3800.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("价格超过涨停价拒绝") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4300.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::PRICE_OUT_OF_LIMIT);
        REQUIRE(result.rejectText.find("Price out of limit") != std::string::npos);
    }
    
    SECTION("价格低于跌停价拒绝") {
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 3700.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::PRICE_OUT_OF_LIMIT);
    }
    
    SECTION("市价单不检查价格") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, inst);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("涨跌停未设置时跳过检查") {
        Instrument instNoLimit("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
        // 不设置涨跌停价格
        
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 10000.0, 1);
        
        CheckResult result = riskMgr.checkPrice(order, instNoLimit);
        
        REQUIRE(result.passed == true);
    }
}

// =============================================================================
// 单元测试 - checkPosition 持仓检查
// =============================================================================

TEST_CASE("RiskManager checkPosition 持仓检查", "[risk_manager][unit]") {
    RiskManager riskMgr;
    
    SECTION("卖出平多头 - 持仓充足") {
        Position position("user001", "IF2601");
        position.longPosition = 5;
        
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 3);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("卖出平多头 - 持仓不足") {
        Position position("user001", "IF2601");
        position.longPosition = 2;
        
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 5);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
        REQUIRE(result.rejectText.find("Insufficient position") != std::string::npos);
    }
    
    SECTION("买入平空头 - 持仓充足") {
        Position position("user001", "IF2601");
        position.shortPosition = 5;
        
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 3);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("买入平空头 - 持仓不足") {
        Position position("user001", "IF2601");
        position.shortPosition = 2;
        
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 5);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
    }
    
    SECTION("平仓数量等于持仓数量") {
        Position position("user001", "IF2601");
        position.longPosition = 3;
        
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 3);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("无持仓时平仓拒绝") {
        Position position("user001", "IF2601");
        // 无持仓
        
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 1);
        
        CheckResult result = riskMgr.checkPosition(order, position);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
    }
}

// =============================================================================
// 单元测试 - checkCounterParty 对手盘检查
// =============================================================================

TEST_CASE("RiskManager checkCounterParty 对手盘检查", "[risk_manager][unit]") {
    RiskManager riskMgr;
    
    SECTION("买单有卖盘时通过") {
        MarketDataSnapshot snapshot = createTestSnapshot();
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkCounterParty(order, snapshot);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("买单无卖盘时拒绝") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 4000.0;
        snapshot.bidVolume1 = 100;
        snapshot.askPrice1 = 0.0;  // 无卖盘
        snapshot.askVolume1 = 0;
        
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkCounterParty(order, snapshot);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::NO_COUNTER_PARTY);
        REQUIRE(result.rejectText.find("No counter party") != std::string::npos);
    }
    
    SECTION("卖单有买盘时通过") {
        MarketDataSnapshot snapshot = createTestSnapshot();
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkCounterParty(order, snapshot);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("卖单无买盘时拒绝") {
        MarketDataSnapshot snapshot("IF2601");
        snapshot.bidPrice1 = 0.0;  // 无买盘
        snapshot.bidVolume1 = 0;
        snapshot.askPrice1 = 4000.0;
        snapshot.askVolume1 = 100;
        
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkCounterParty(order, snapshot);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::NO_COUNTER_PARTY);
    }
    
    SECTION("限价单不检查对手盘") {
        MarketDataSnapshot snapshot("IF2601");
        // 无买卖盘
        
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 1);
        
        CheckResult result = riskMgr.checkCounterParty(order, snapshot);
        
        REQUIRE(result.passed == true);
    }
}

// =============================================================================
// 单元测试 - checkOrder 完整检查
// =============================================================================

TEST_CASE("RiskManager checkOrder 完整检查", "[risk_manager][unit]") {
    RiskManager riskMgr;
    Instrument inst = createTestInstrument();
    MarketDataSnapshot snapshot = createTestSnapshot();
    
    SECTION("开仓限价单 - 全部通过") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 2);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::OPEN);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("开仓限价单 - 价格超限") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4500.0, 2);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::OPEN);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::PRICE_OUT_OF_LIMIT);
    }
    
    SECTION("开仓限价单 - 资金不足") {
        Account account("user001", 100000.0);
        Position position("user001", "IF2601");
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 2);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::OPEN);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_FUNDS);
    }
    
    SECTION("平仓限价单 - 全部通过") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        position.longPosition = 5;
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 2);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::CLOSE);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("平仓限价单 - 持仓不足") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        position.longPosition = 1;
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::LIMIT, 4000.0, 5);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::CLOSE);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
    }
    
    SECTION("开仓市价单 - 全部通过") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, snapshot, OffsetFlag::OPEN);
        
        REQUIRE(result.passed == true);
    }
    
    SECTION("开仓市价单 - 无对手盘") {
        Account account("user001", 1000000.0);
        Position position("user001", "IF2601");
        MarketDataSnapshot emptySnapshot("IF2601");
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        CheckResult result = riskMgr.checkOrder(order, account, position, inst, emptySnapshot, OffsetFlag::OPEN);
        
        REQUIRE(result.passed == false);
        REQUIRE(result.rejectReason == RejectReason::NO_COUNTER_PARTY);
    }
}

// =============================================================================
// 单元测试 - calculateRequiredMargin 保证金计算
// =============================================================================

TEST_CASE("RiskManager calculateRequiredMargin 保证金计算", "[risk_manager][unit]") {
    RiskManager riskMgr;
    Instrument inst = createTestInstrument();  // 乘数300，保证金率12%
    
    SECTION("限价单保证金计算") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::LIMIT, 4000.0, 2);
        
        double margin = riskMgr.calculateRequiredMargin(order, inst);
        
        // 4000 * 2 * 300 * 0.12 = 288000
        REQUIRE(std::abs(margin - 288000.0) < 0.01);
    }
    
    SECTION("市价买单使用涨停价") {
        Order order = createTestOrder("IF2601", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
        
        double margin = riskMgr.calculateRequiredMargin(order, inst);
        
        // 4200 * 1 * 300 * 0.12 = 151200
        REQUIRE(std::abs(margin - 151200.0) < 0.01);
    }
    
    SECTION("市价卖单使用跌停价") {
        Order order = createTestOrder("IF2601", OrderSide::SELL, OrderType::MARKET, 0.0, 1);
        
        double margin = riskMgr.calculateRequiredMargin(order, inst);
        
        // 3800 * 1 * 300 * 0.12 = 136800
        REQUIRE(std::abs(margin - 136800.0) < 0.01);
    }
}


// =============================================================================
// 属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 10: 风控资金检查正确性**
 * **Validates: Requirements 9.1**
 *
 * 对于任意订单和账户状态，当可用资金 < 所需保证金时应拒绝订单。
 */
TEST_CASE("RiskManager 属性测试 - 资金检查", "[risk_manager][property]") {
    
    rc::prop("可用资金不足时应拒绝订单",
        []() {
            RiskManager riskMgr;
            
            // 生成合约参数
            auto volumeMultiple = *rc::gen::inRange(10, 500);
            auto marginRateInt = *rc::gen::inRange(5, 20);  // 5-20%
            double marginRate = marginRateInt / 100.0;
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, volumeMultiple, marginRate);
            inst.updateLimitPrices(5000.0, 3000.0);
            
            // 生成订单参数
            auto priceInt = *rc::gen::inRange(3000, 5000);
            double price = static_cast<double>(priceInt);
            auto qty = *rc::gen::inRange(1, 10);
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, price, qty);
            
            // 计算所需保证金
            double requiredMargin = price * qty * volumeMultiple * marginRate;
            
            // 生成不足的可用资金（0% - 99% 的所需保证金）
            auto availableRatio = *rc::gen::inRange(0, 99);
            double available = requiredMargin * availableRatio / 100.0;
            
            Account account("test", available);
            
            // 执行检查
            CheckResult result = riskMgr.checkMargin(order, account, inst);
            
            // 验证：资金不足时应拒绝
            RC_ASSERT(!result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::INSUFFICIENT_FUNDS);
        });
    
    rc::prop("可用资金充足时应通过",
        []() {
            RiskManager riskMgr;
            
            // 生成合约参数
            auto volumeMultiple = *rc::gen::inRange(10, 500);
            auto marginRateInt = *rc::gen::inRange(5, 20);
            double marginRate = marginRateInt / 100.0;
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, volumeMultiple, marginRate);
            inst.updateLimitPrices(5000.0, 3000.0);
            
            // 生成订单参数
            auto priceInt = *rc::gen::inRange(3000, 5000);
            double price = static_cast<double>(priceInt);
            auto qty = *rc::gen::inRange(1, 10);
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, price, qty);
            
            // 计算所需保证金
            double requiredMargin = price * qty * volumeMultiple * marginRate;
            
            // 生成充足的可用资金（101% - 200% 的所需保证金）
            // 使用101%而非100%，避免浮点数精度问题导致边界情况失败
            auto availableRatio = *rc::gen::inRange(101, 200);
            double available = requiredMargin * availableRatio / 100.0;
            
            Account account("test", available);
            
            // 执行检查
            CheckResult result = riskMgr.checkMargin(order, account, inst);
            
            // 验证：资金充足时应通过
            RC_ASSERT(result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::NONE);
        });
    
    rc::prop("保证金计算公式正确",
        []() {
            RiskManager riskMgr;
            
            // 生成合约参数
            auto volumeMultiple = *rc::gen::inRange(10, 500);
            auto marginRateInt = *rc::gen::inRange(5, 20);
            double marginRate = marginRateInt / 100.0;
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, volumeMultiple, marginRate);
            inst.updateLimitPrices(5000.0, 3000.0);
            
            // 生成订单参数
            auto priceInt = *rc::gen::inRange(3000, 5000);
            double price = static_cast<double>(priceInt);
            auto qty = *rc::gen::inRange(1, 10);
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, price, qty);
            
            // 计算期望的保证金
            double expectedMargin = price * qty * volumeMultiple * marginRate;
            
            // 计算实际的保证金
            double actualMargin = riskMgr.calculateRequiredMargin(order, inst);
            
            // 验证：保证金计算正确
            RC_ASSERT(std::abs(actualMargin - expectedMargin) < 0.01);
        });
}

/**
 * **Feature: paper-trading-system, Property 11: 风控价格检查正确性**
 * **Validates: Requirements 9.2**
 *
 * 对于任意限价单和合约涨跌停价，当价格超出涨跌停范围时应拒绝订单。
 */
TEST_CASE("RiskManager 属性测试 - 价格检查", "[risk_manager][property]") {
    
    rc::prop("价格超过涨停价时应拒绝",
        []() {
            RiskManager riskMgr;
            
            // 生成涨跌停价格
            auto lowerLimitInt = *rc::gen::inRange(3000, 4000);
            auto upperLimitInt = *rc::gen::inRange(4500, 5500);
            double lowerLimit = static_cast<double>(lowerLimitInt);
            double upperLimit = static_cast<double>(upperLimitInt);
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, 300, 0.12);
            inst.updateLimitPrices(upperLimit, lowerLimit);
            
            // 生成超过涨停价的价格
            auto excessInt = *rc::gen::inRange(1, 500);
            double price = upperLimit + excessInt;
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, price, 1);
            
            // 执行检查
            CheckResult result = riskMgr.checkPrice(order, inst);
            
            // 验证：价格超限时应拒绝
            RC_ASSERT(!result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::PRICE_OUT_OF_LIMIT);
        });
    
    rc::prop("价格低于跌停价时应拒绝",
        []() {
            RiskManager riskMgr;
            
            // 生成涨跌停价格
            auto lowerLimitInt = *rc::gen::inRange(3000, 4000);
            auto upperLimitInt = *rc::gen::inRange(4500, 5500);
            double lowerLimit = static_cast<double>(lowerLimitInt);
            double upperLimit = static_cast<double>(upperLimitInt);
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, 300, 0.12);
            inst.updateLimitPrices(upperLimit, lowerLimit);
            
            // 生成低于跌停价的价格
            auto deficitInt = *rc::gen::inRange(1, 500);
            double price = lowerLimit - deficitInt;
            
            Order order = createTestOrder("TEST", OrderSide::SELL, OrderType::LIMIT, price, 1);
            
            // 执行检查
            CheckResult result = riskMgr.checkPrice(order, inst);
            
            // 验证：价格超限时应拒绝
            RC_ASSERT(!result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::PRICE_OUT_OF_LIMIT);
        });
    
    rc::prop("价格在涨跌停范围内时应通过",
        []() {
            RiskManager riskMgr;
            
            // 生成涨跌停价格
            auto lowerLimitInt = *rc::gen::inRange(3000, 4000);
            auto upperLimitInt = *rc::gen::inRange(4500, 5500);
            double lowerLimit = static_cast<double>(lowerLimitInt);
            double upperLimit = static_cast<double>(upperLimitInt);
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, 300, 0.12);
            inst.updateLimitPrices(upperLimit, lowerLimit);
            
            // 生成在范围内的价格
            auto priceInt = *rc::gen::inRange(lowerLimitInt, upperLimitInt);
            double price = static_cast<double>(priceInt);
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, price, 1);
            
            // 执行检查
            CheckResult result = riskMgr.checkPrice(order, inst);
            
            // 验证：价格在范围内时应通过
            RC_ASSERT(result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::NONE);
        });
    
    rc::prop("市价单不检查价格",
        []() {
            RiskManager riskMgr;
            
            // 生成涨跌停价格
            auto lowerLimitInt = *rc::gen::inRange(3000, 4000);
            auto upperLimitInt = *rc::gen::inRange(4500, 5500);
            double lowerLimit = static_cast<double>(lowerLimitInt);
            double upperLimit = static_cast<double>(upperLimitInt);
            
            Instrument inst("TEST", "CFFEX", "T", 0.2, 300, 0.12);
            inst.updateLimitPrices(upperLimit, lowerLimit);
            
            // 市价单价格为0
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::MARKET, 0.0, 1);
            
            // 执行检查
            CheckResult result = riskMgr.checkPrice(order, inst);
            
            // 验证：市价单应通过价格检查
            RC_ASSERT(result.passed);
        });
}

/**
 * **Feature: paper-trading-system, Property 12: 风控持仓检查正确性**
 * **Validates: Requirements 9.3**
 *
 * 对于任意平仓订单和持仓状态，当平仓数量 > 持仓数量时应拒绝订单。
 */
TEST_CASE("RiskManager 属性测试 - 持仓检查", "[risk_manager][property]") {
    
    rc::prop("平仓数量超过持仓时应拒绝（卖出平多头）",
        []() {
            RiskManager riskMgr;
            
            // 生成持仓数量
            auto positionQty = *rc::gen::inRange(1, 100);
            
            Position position("test", "TEST");
            position.longPosition = positionQty;
            
            // 生成超过持仓的平仓数量
            auto excessQty = *rc::gen::inRange(1, 50);
            int64_t orderQty = positionQty + excessQty;
            
            Order order = createTestOrder("TEST", OrderSide::SELL, OrderType::LIMIT, 4000.0, orderQty);
            
            // 执行检查
            CheckResult result = riskMgr.checkPosition(order, position);
            
            // 验证：持仓不足时应拒绝
            RC_ASSERT(!result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
        });
    
    rc::prop("平仓数量超过持仓时应拒绝（买入平空头）",
        []() {
            RiskManager riskMgr;
            
            // 生成持仓数量
            auto positionQty = *rc::gen::inRange(1, 100);
            
            Position position("test", "TEST");
            position.shortPosition = positionQty;
            
            // 生成超过持仓的平仓数量
            auto excessQty = *rc::gen::inRange(1, 50);
            int64_t orderQty = positionQty + excessQty;
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, 4000.0, orderQty);
            
            // 执行检查
            CheckResult result = riskMgr.checkPosition(order, position);
            
            // 验证：持仓不足时应拒绝
            RC_ASSERT(!result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::INSUFFICIENT_POSITION);
        });
    
    rc::prop("平仓数量不超过持仓时应通过（卖出平多头）",
        []() {
            RiskManager riskMgr;
            
            // 生成持仓数量（最小为2，避免inRange(1,1)空范围问题）
            auto positionQty = *rc::gen::inRange(2, 101);
            
            Position position("test", "TEST");
            position.longPosition = positionQty;
            
            // 生成不超过持仓的平仓数量（使用positionQty+1确保包含上界）
            auto orderQty = *rc::gen::inRange(1, positionQty + 1);
            
            Order order = createTestOrder("TEST", OrderSide::SELL, OrderType::LIMIT, 4000.0, orderQty);
            
            // 执行检查
            CheckResult result = riskMgr.checkPosition(order, position);
            
            // 验证：持仓充足时应通过
            RC_ASSERT(result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::NONE);
        });
    
    rc::prop("平仓数量不超过持仓时应通过（买入平空头）",
        []() {
            RiskManager riskMgr;
            
            // 生成持仓数量（最小为2，避免inRange(1,1)空范围问题）
            auto positionQty = *rc::gen::inRange(2, 101);
            
            Position position("test", "TEST");
            position.shortPosition = positionQty;
            
            // 生成不超过持仓的平仓数量（使用positionQty+1确保包含上界）
            auto orderQty = *rc::gen::inRange(1, positionQty + 1);
            
            Order order = createTestOrder("TEST", OrderSide::BUY, OrderType::LIMIT, 4000.0, orderQty);
            
            // 执行检查
            CheckResult result = riskMgr.checkPosition(order, position);
            
            // 验证：持仓充足时应通过
            RC_ASSERT(result.passed);
            RC_ASSERT(result.rejectReason == RejectReason::NONE);
        });
    
    rc::prop("平仓数量等于持仓数量时应通过",
        []() {
            RiskManager riskMgr;
            
            // 生成持仓数量
            auto positionQty = *rc::gen::inRange(1, 100);
            
            Position position("test", "TEST");
            position.longPosition = positionQty;
            
            // 平仓数量等于持仓数量
            Order order = createTestOrder("TEST", OrderSide::SELL, OrderType::LIMIT, 4000.0, positionQty);
            
            // 执行检查
            CheckResult result = riskMgr.checkPosition(order, position);
            
            // 验证：应通过
            RC_ASSERT(result.passed);
        });
}
