#include "../catch2/catch.hpp"
#include "market/market_data.hpp"
#include "market/mock_md_adapter.hpp"
#include <thread>
#include <chrono>

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
