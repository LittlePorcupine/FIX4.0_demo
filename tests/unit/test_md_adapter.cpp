#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "market/market_data.hpp"
#include "market/mock_md_adapter.hpp"
#include "app/model/market_data_snapshot.hpp"
#include <thread>
#include <chrono>
#include <cmath>

using namespace fix40;
using namespace std::chrono_literals;

// ============================================================================
// MarketData Tests
// ============================================================================

TEST_CASE("MarketData - Default construction", "[market_data]") {
    MarketData md;
    
    REQUIRE(md.instrumentID[0] == '\0');
    REQUIRE(md.exchangeID[0] == '\0');
    REQUIRE(md.lastPrice == 0.0);
    REQUIRE(md.volume == 0);
    REQUIRE(md.bidPrice1 == 0.0);
    REQUIRE(md.askPrice1 == 0.0);
}

TEST_CASE("MarketData - Set instrument ID", "[market_data]") {
    MarketData md;
    md.setInstrumentID("IF2401");
    
    REQUIRE(std::string(md.instrumentID) == "IF2401");
}

TEST_CASE("MarketData - Set exchange ID", "[market_data]") {
    MarketData md;
    md.setExchangeID("CFFEX");
    
    REQUIRE(std::string(md.exchangeID) == "CFFEX");
}

TEST_CASE("MarketData - Set trading day", "[market_data]") {
    MarketData md;
    md.setTradingDay("20241209");
    
    REQUIRE(std::string(md.tradingDay) == "20241209");
}

TEST_CASE("MarketData - Set update time", "[market_data]") {
    MarketData md;
    md.setUpdateTime("09:30:00");
    
    REQUIRE(std::string(md.updateTime) == "09:30:00");
}

TEST_CASE("MarketData - Long instrument ID truncation", "[market_data]") {
    MarketData md;
    std::string longId(100, 'X');  // 100 characters
    md.setInstrumentID(longId.c_str());
    
    // Should be truncated to INSTRUMENT_ID_LEN - 1
    REQUIRE(std::strlen(md.instrumentID) == INSTRUMENT_ID_LEN - 1);
}

TEST_CASE("MarketData - Is trivially copyable", "[market_data]") {
    REQUIRE(std::is_trivially_copyable<MarketData>::value);
}

TEST_CASE("MarketData - Copy semantics", "[market_data]") {
    MarketData md1;
    md1.setInstrumentID("IF2401");
    md1.lastPrice = 5000.0;
    md1.volume = 1000;
    
    MarketData md2 = md1;  // Copy
    
    REQUIRE(std::string(md2.instrumentID) == "IF2401");
    REQUIRE(md2.lastPrice == 5000.0);
    REQUIRE(md2.volume == 1000);
}

TEST_CASE("MarketData - getInstrumentID", "[market_data]") {
    MarketData md;
    md.setInstrumentID("IC2401");
    
    REQUIRE(md.getInstrumentID() == "IC2401");
}

TEST_CASE("MarketData - getExchangeID", "[market_data]") {
    MarketData md;
    md.setExchangeID("SHFE");
    
    REQUIRE(md.getExchangeID() == "SHFE");
}

// ============================================================================
// MockMdAdapter Tests
// ============================================================================

TEST_CASE("MockMdAdapter - Construction", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    REQUIRE(adapter.getName() == "Mock");
    REQUIRE(adapter.getState() == MdAdapterState::DISCONNECTED);
    REQUIRE_FALSE(adapter.isRunning());
}

TEST_CASE("MockMdAdapter - Start and stop", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    REQUIRE(adapter.start());
    REQUIRE(adapter.isRunning());
    REQUIRE(adapter.getState() == MdAdapterState::READY);
    
    adapter.stop();
    REQUIRE_FALSE(adapter.isRunning());
    REQUIRE(adapter.getState() == MdAdapterState::DISCONNECTED);
}

TEST_CASE("MockMdAdapter - Double start is safe", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    REQUIRE(adapter.start());
    REQUIRE(adapter.start());  // Should return true without error
    REQUIRE(adapter.isRunning());
    
    adapter.stop();
}

TEST_CASE("MockMdAdapter - Double stop is safe", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.start();
    adapter.stop();
    adapter.stop();  // Should not crash
    
    REQUIRE_FALSE(adapter.isRunning());
}

TEST_CASE("MockMdAdapter - Trading day is set", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.start();
    std::string tradingDay = adapter.getTradingDay();
    adapter.stop();
    
    REQUIRE(tradingDay.length() == 8);  // YYYYMMDD format
}

TEST_CASE("MockMdAdapter - Subscribe before start fails", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    REQUIRE_FALSE(adapter.subscribe({"IF2401"}));
}

TEST_CASE("MockMdAdapter - Subscribe after start succeeds", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.start();
    REQUIRE(adapter.subscribe({"IF2401", "IC2401"}));
    adapter.stop();
}

TEST_CASE("MockMdAdapter - Unsubscribe", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.start();
    adapter.subscribe({"IF2401", "IC2401"});
    REQUIRE(adapter.unsubscribe({"IF2401"}));
    adapter.stop();
}

TEST_CASE("MockMdAdapter - Generates market data", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.setTickInterval(50ms);  // Fast ticks for testing
    adapter.start();
    adapter.subscribe({"IF2401"});
    
    // Wait for some data
    std::this_thread::sleep_for(200ms);
    
    adapter.stop();
    
    // Should have received some market data
    MarketData md;
    REQUIRE(queue.try_dequeue(md));
    REQUIRE(std::string(md.instrumentID) == "IF2401");
    REQUIRE(std::string(md.exchangeID) == "MOCK");
    REQUIRE(md.lastPrice > 0.0);
}

TEST_CASE("MockMdAdapter - Set base price", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.setBasePrice("IF2401", 4000.0);
    adapter.setTickInterval(50ms);
    adapter.start();
    adapter.subscribe({"IF2401"});
    
    std::this_thread::sleep_for(200ms);
    adapter.stop();
    
    MarketData md;
    REQUIRE(queue.try_dequeue(md));
    
    // Price should be around base price (within 10%)
    REQUIRE(md.lastPrice >= 3600.0);  // 4000 * 0.9
    REQUIRE(md.lastPrice <= 4400.0);  // 4000 * 1.1
}

TEST_CASE("MockMdAdapter - State callback", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    std::vector<MdAdapterState> states;
    adapter.setStateCallback([&states](MdAdapterState state, const std::string&) {
        states.push_back(state);
    });
    
    adapter.start();
    adapter.stop();
    
    // Should have received state changes
    REQUIRE(states.size() >= 2);
    REQUIRE(states.back() == MdAdapterState::DISCONNECTED);
}

TEST_CASE("MockMdAdapter - Multiple instruments", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.setTickInterval(50ms);
    adapter.start();
    adapter.subscribe({"IF2401", "IC2401", "IH2401"});
    
    std::this_thread::sleep_for(300ms);
    adapter.stop();
    
    // Collect all received instruments
    std::set<std::string> receivedInstruments;
    MarketData md;
    while (queue.try_dequeue(md)) {
        receivedInstruments.insert(md.getInstrumentID());
    }
    
    // Should have received data for all subscribed instruments
    REQUIRE(receivedInstruments.count("IF2401") > 0);
    REQUIRE(receivedInstruments.count("IC2401") > 0);
    REQUIRE(receivedInstruments.count("IH2401") > 0);
}

TEST_CASE("MockMdAdapter - Market data has valid bid/ask", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.setTickInterval(50ms);
    adapter.start();
    adapter.subscribe({"IF2401"});
    
    std::this_thread::sleep_for(200ms);
    adapter.stop();
    
    MarketData md;
    REQUIRE(queue.try_dequeue(md));
    
    // Bid should be less than ask
    REQUIRE(md.bidPrice1 < md.askPrice1);
    REQUIRE(md.bidVolume1 > 0);
    REQUIRE(md.askVolume1 > 0);
    
    // 5 levels of depth
    REQUIRE(md.bidPrice2 < md.bidPrice1);
    REQUIRE(md.askPrice2 > md.askPrice1);
}

TEST_CASE("MockMdAdapter - Market data has valid time", "[mock_md_adapter]") {
    moodycamel::BlockingConcurrentQueue<MarketData> queue;
    MockMdAdapter adapter(queue);
    
    adapter.setTickInterval(50ms);
    adapter.start();
    adapter.subscribe({"IF2401"});
    
    std::this_thread::sleep_for(200ms);
    adapter.stop();
    
    MarketData md;
    REQUIRE(queue.try_dequeue(md));
    
    // Update time should be in HH:MM:SS format
    std::string updateTime(md.updateTime);
    REQUIRE(updateTime.length() == 8);
    REQUIRE(updateTime[2] == ':');
    REQUIRE(updateTime[5] == ':');
}


// ============================================================================
// RapidCheck 属性测试 - 行情数据转换一致性
// ============================================================================

/**
 * @brief MarketData 生成器
 * 
 * 生成有效的 MarketData 对象用于属性测试。
 * 确保生成的数据符合真实行情的约束条件。
 */
namespace rc {
template<>
struct Arbitrary<MarketData> {
    static Gen<MarketData> arbitrary() {
        return gen::exec([]() {
            MarketData md;
            
            // 生成合约代码（使用预定义的合约代码列表）
            auto instrumentId = *gen::elementOf(std::vector<std::string>{
                "IF2401", "IF2402", "IF2403", "IC2401", "IC2402", 
                "IH2401", "IH2402", "cu2401", "au2401", "rb2401"
            });
            md.setInstrumentID(instrumentId.c_str());
            
            // 生成交易所代码
            auto exchangeId = *gen::elementOf(std::vector<std::string>{
                "CFFEX", "SHFE", "DCE", "CZCE", "INE"
            });
            md.setExchangeID(exchangeId.c_str());
            
            // 生成交易日 (YYYYMMDD)
            md.setTradingDay("20241216");
            
            // 生成更新时间 (HH:MM:SS)
            int hour = *gen::inRange(9, 16);
            int minute = *gen::inRange(0, 60);
            int second = *gen::inRange(0, 60);
            char timeStr[16];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour, minute, second);
            md.setUpdateTime(timeStr);
            md.updateMillisec = *gen::inRange(0, 1000);
            
            // 生成价格信息（确保价格为正数且合理）
            int basePriceInt = *gen::inRange(1000, 10000);  // 基准价格（整数）
            double basePrice = static_cast<double>(basePriceInt);
            double tickSize = *gen::elementOf(std::vector<double>{0.2, 0.5, 1.0, 2.0, 5.0});
            
            // 最新价在基准价格附近波动
            int priceOffset = *gen::inRange(-100, 101);
            md.lastPrice = basePrice + priceOffset * tickSize;
            
            // 涨跌停价格
            md.upperLimitPrice = basePrice * 1.1;  // 涨停 +10%
            md.lowerLimitPrice = basePrice * 0.9;  // 跌停 -10%
            
            // 昨结算价
            md.preSettlementPrice = basePrice;
            int preCloseOffset = *gen::inRange(-50, 51);
            md.preClosePrice = basePrice + preCloseOffset * tickSize;
            
            // 开高低收
            int openOffset = *gen::inRange(-30, 31);
            md.openPrice = basePrice + openOffset * tickSize;
            int highOffset = *gen::inRange(0, 51);
            md.highestPrice = md.lastPrice + highOffset * tickSize;
            int lowOffset = *gen::inRange(0, 51);
            md.lowestPrice = md.lastPrice - lowOffset * tickSize;
            md.closePrice = 0.0;  // 收盘前为0
            md.settlementPrice = 0.0;  // 结算前为0
            
            // 成交信息
            md.volume = *gen::inRange<int64_t>(0, 1000000);
            int multiplier = *gen::inRange(1, 301);
            md.turnover = md.volume * md.lastPrice * multiplier;  // 假设乘数1-300
            int openInterestInt = *gen::inRange(0, 500000);
            md.openInterest = static_cast<double>(openInterestInt);
            int oiOffset = *gen::inRange(-10000, 10001);
            md.preOpenInterest = md.openInterest + oiOffset;
            
            // 买卖盘口（确保买价 < 卖价）
            int spread = *gen::inRange(1, 11);  // 价差1-10个tick
            
            // 买一价略低于最新价
            int bidOffset = *gen::inRange(0, 3);
            md.bidPrice1 = md.lastPrice - bidOffset * tickSize;
            md.bidVolume1 = *gen::inRange<int32_t>(1, 1001);
            
            // 卖一价 = 买一价 + 价差
            md.askPrice1 = md.bidPrice1 + spread * tickSize;
            md.askVolume1 = *gen::inRange<int32_t>(1, 1001);
            
            // 其他档位
            md.bidPrice2 = md.bidPrice1 - tickSize;
            md.bidVolume2 = *gen::inRange<int32_t>(1, 501);
            md.askPrice2 = md.askPrice1 + tickSize;
            md.askVolume2 = *gen::inRange<int32_t>(1, 501);
            
            md.bidPrice3 = md.bidPrice2 - tickSize;
            md.bidVolume3 = *gen::inRange<int32_t>(1, 501);
            md.askPrice3 = md.askPrice2 + tickSize;
            md.askVolume3 = *gen::inRange<int32_t>(1, 501);
            
            md.bidPrice4 = md.bidPrice3 - tickSize;
            md.bidVolume4 = *gen::inRange<int32_t>(1, 501);
            md.askPrice4 = md.askPrice3 + tickSize;
            md.askVolume4 = *gen::inRange<int32_t>(1, 501);
            
            md.bidPrice5 = md.bidPrice4 - tickSize;
            md.bidVolume5 = *gen::inRange<int32_t>(1, 501);
            md.askPrice5 = md.askPrice4 + tickSize;
            md.askVolume5 = *gen::inRange<int32_t>(1, 501);
            
            return md;
        });
    }
};
} // namespace rc

/**
 * @brief 将 MarketData 转换为 MarketDataSnapshot
 * 
 * 这是撮合引擎中使用的转换逻辑的模拟。
 * 实际转换发生在 MatchingEngine::handleMarketData 中。
 */
MarketDataSnapshot convertToSnapshot(const MarketData& md) {
    MarketDataSnapshot snapshot;
    snapshot.instrumentId = md.getInstrumentID();
    snapshot.lastPrice = md.lastPrice;
    snapshot.bidPrice1 = md.bidPrice1;
    snapshot.bidVolume1 = md.bidVolume1;
    snapshot.askPrice1 = md.askPrice1;
    snapshot.askVolume1 = md.askVolume1;
    snapshot.upperLimitPrice = md.upperLimitPrice;
    snapshot.lowerLimitPrice = md.lowerLimitPrice;
    snapshot.updateTime = std::chrono::system_clock::now();
    return snapshot;
}

/**
 * **Feature: paper-trading-system, Property 1: 行情数据转换一致性**
 * **Validates: Requirements 1.3**
 * 
 * 对于任意 CTP 深度行情数据，转换为内部 MarketData 格式后，
 * 关键字段（合约代码、买卖盘价格、买卖盘数量）应与原始数据一致。
 */
TEST_CASE("行情数据转换属性测试", "[market_data][property]") {
    
    rc::prop("MarketData 到 MarketDataSnapshot 转换保持关键字段一致",
        [](const MarketData& md) {
            MarketDataSnapshot snapshot = convertToSnapshot(md);
            
            // 验证合约代码一致
            RC_ASSERT(snapshot.instrumentId == md.getInstrumentID());
            
            // 验证最新价一致
            RC_ASSERT(snapshot.lastPrice == md.lastPrice);
            
            // 验证买一价和买一量一致
            RC_ASSERT(snapshot.bidPrice1 == md.bidPrice1);
            RC_ASSERT(snapshot.bidVolume1 == md.bidVolume1);
            
            // 验证卖一价和卖一量一致
            RC_ASSERT(snapshot.askPrice1 == md.askPrice1);
            RC_ASSERT(snapshot.askVolume1 == md.askVolume1);
            
            // 验证涨跌停价格一致
            RC_ASSERT(snapshot.upperLimitPrice == md.upperLimitPrice);
            RC_ASSERT(snapshot.lowerLimitPrice == md.lowerLimitPrice);
        });
    
    rc::prop("转换后的行情快照有效性与原始数据一致",
        [](const MarketData& md) {
            MarketDataSnapshot snapshot = convertToSnapshot(md);
            
            // 如果原始数据有买盘，转换后也应该有买盘
            bool originalHasBid = (md.bidPrice1 > 0 && md.bidVolume1 > 0);
            RC_ASSERT(snapshot.hasBid() == originalHasBid);
            
            // 如果原始数据有卖盘，转换后也应该有卖盘
            bool originalHasAsk = (md.askPrice1 > 0 && md.askVolume1 > 0);
            RC_ASSERT(snapshot.hasAsk() == originalHasAsk);
            
            // 如果原始数据有效，转换后也应该有效
            bool originalIsValid = (md.bidPrice1 > 0 || md.askPrice1 > 0);
            RC_ASSERT(snapshot.isValid() == originalIsValid);
        });
    
    rc::prop("MarketData 复制保持所有字段一致",
        [](const MarketData& md) {
            MarketData copy = md;  // 复制
            
            // 验证所有关键字段一致
            RC_ASSERT(std::string(copy.instrumentID) == std::string(md.instrumentID));
            RC_ASSERT(std::string(copy.exchangeID) == std::string(md.exchangeID));
            RC_ASSERT(std::string(copy.tradingDay) == std::string(md.tradingDay));
            RC_ASSERT(std::string(copy.updateTime) == std::string(md.updateTime));
            RC_ASSERT(copy.updateMillisec == md.updateMillisec);
            
            RC_ASSERT(copy.lastPrice == md.lastPrice);
            RC_ASSERT(copy.preSettlementPrice == md.preSettlementPrice);
            RC_ASSERT(copy.preClosePrice == md.preClosePrice);
            RC_ASSERT(copy.openPrice == md.openPrice);
            RC_ASSERT(copy.highestPrice == md.highestPrice);
            RC_ASSERT(copy.lowestPrice == md.lowestPrice);
            RC_ASSERT(copy.upperLimitPrice == md.upperLimitPrice);
            RC_ASSERT(copy.lowerLimitPrice == md.lowerLimitPrice);
            
            RC_ASSERT(copy.volume == md.volume);
            RC_ASSERT(copy.turnover == md.turnover);
            RC_ASSERT(copy.openInterest == md.openInterest);
            
            RC_ASSERT(copy.bidPrice1 == md.bidPrice1);
            RC_ASSERT(copy.bidVolume1 == md.bidVolume1);
            RC_ASSERT(copy.askPrice1 == md.askPrice1);
            RC_ASSERT(copy.askVolume1 == md.askVolume1);
        });
    
    rc::prop("生成的行情数据满足基本约束",
        [](const MarketData& md) {
            // 买一价应小于卖一价（正常行情）
            if (md.bidPrice1 > 0 && md.askPrice1 > 0) {
                RC_ASSERT(md.bidPrice1 < md.askPrice1);
            }
            
            // 买卖量应为非负
            RC_ASSERT(md.bidVolume1 >= 0);
            RC_ASSERT(md.askVolume1 >= 0);
            
            // 涨停价应大于跌停价
            if (md.upperLimitPrice > 0 && md.lowerLimitPrice > 0) {
                RC_ASSERT(md.upperLimitPrice > md.lowerLimitPrice);
            }
        });
}

/**
 * 测试 MarketDataSnapshot 的辅助方法
 */
TEST_CASE("MarketDataSnapshot 辅助方法属性测试", "[market_data_snapshot][property]") {
    
    rc::prop("价差计算正确性",
        [](const MarketData& md) {
            MarketDataSnapshot snapshot = convertToSnapshot(md);
            
            if (snapshot.bidPrice1 > 0 && snapshot.askPrice1 > 0) {
                double expectedSpread = snapshot.askPrice1 - snapshot.bidPrice1;
                RC_ASSERT(std::abs(snapshot.getSpread() - expectedSpread) < 1e-9);
            } else {
                RC_ASSERT(snapshot.getSpread() == 0.0);
            }
        });
    
    rc::prop("中间价计算正确性",
        [](const MarketData& md) {
            MarketDataSnapshot snapshot = convertToSnapshot(md);
            
            if (snapshot.bidPrice1 > 0 && snapshot.askPrice1 > 0) {
                double expectedMid = (snapshot.bidPrice1 + snapshot.askPrice1) / 2.0;
                RC_ASSERT(std::abs(snapshot.getMidPrice() - expectedMid) < 1e-9);
            } else {
                // 无有效买卖盘时返回最新价
                RC_ASSERT(snapshot.getMidPrice() == snapshot.lastPrice);
            }
        });
}
