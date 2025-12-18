#include "../catch2/catch.hpp"
#include "app/simulation_app.hpp"
#include "fix/fix_tags.hpp"
#include "market/market_data.hpp"
#include "storage/sqlite_store.hpp"
#include <chrono>
#include <thread>

using namespace fix40;
using namespace std::chrono_literals;

namespace {

template<typename Predicate>
bool waitFor(Predicate pred, std::chrono::milliseconds timeout = 1000ms) {
    auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

} // namespace

TEST_CASE("SimulationApp - order/trade persistence", "[application][storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SimulationApp app(&store);

    // 添加测试合约（保证金等规则）
    Instrument inst("TEST", "TESTEX", "T", 1.0, 1, 0.1);
    app.getInstrumentManager().addInstrument(inst);

    // 注册一个已“建立”的 server-side session，确保 extractAccountId() 可用
    auto session = std::make_shared<Session>("SERVER", "CLIENT1", 30, []() {});
    session->set_client_comp_id("CLIENT1");
    app.getSessionManager().registerSession(session);
    SessionID sid = session->get_session_id();

    app.start();

    // 先注入行情，确保撮合引擎与风控能拿到有效快照
    MarketData md;
    md.setInstrumentID("TEST");
    md.lastPrice = 100.0;
    md.bidPrice1 = 99.0;
    md.bidVolume1 = 10;
    md.askPrice1 = 100.0;
    md.askVolume1 = 10;
    md.upperLimitPrice = 200.0;
    md.lowerLimitPrice = 50.0;

    app.getMatchingEngine().submitMarketData(md);
    REQUIRE(waitFor([&]() { return app.getMatchingEngine().getMarketSnapshot("TEST") != nullptr; }));

    // 发送一个可立即成交的限价买单（买价 >= 卖一价）
    FixMessage order;
    order.set(tags::MsgType, "D");
    order.set(tags::ClOrdID, "ORD-PERSIST-001");
    order.set(tags::Symbol, "TEST");
    order.set(tags::Side, "1");
    order.set(tags::OrderQty, "2");
    order.set(tags::OrdType, "2");
    order.set(tags::Price, "100");

    app.fromApp(order, sid);

    // 等待订单状态推进到 FILLED，并写入 orderID
    REQUIRE(waitFor([&]() {
        auto loaded = store.loadOrder("ORD-PERSIST-001");
        return loaded.has_value() &&
               !loaded->orderID.empty() &&
               loaded->status == OrderStatus::FILLED &&
               loaded->cumQty == 2 &&
               loaded->leavesQty == 0;
    }));

    // 校验成交已落库
    auto trades = store.loadTradesByOrder("ORD-PERSIST-001");
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].tradeId.rfind("EXEC-", 0) == 0);
    REQUIRE(trades[0].symbol == "TEST");
    REQUIRE(trades[0].quantity == 2);

    app.stop();
}

TEST_CASE("SimulationApp - rejected order persists status without orderID", "[application][storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SimulationApp app(&store);

    // 添加测试合约
    Instrument inst("TEST", "TESTEX", "T", 1.0, 1, 0.1);
    app.getInstrumentManager().addInstrument(inst);

    auto session = std::make_shared<Session>("SERVER", "CLIENT1", 30, []() {});
    session->set_client_comp_id("CLIENT1");
    app.getSessionManager().registerSession(session);
    SessionID sid = session->get_session_id();

    app.start();

    // 注入带涨跌停的行情，用于价格风控
    MarketData md;
    md.setInstrumentID("TEST");
    md.lastPrice = 100.0;
    md.bidPrice1 = 99.0;
    md.bidVolume1 = 10;
    md.askPrice1 = 100.0;
    md.askVolume1 = 10;
    md.upperLimitPrice = 200.0;
    md.lowerLimitPrice = 50.0;

    app.getMatchingEngine().submitMarketData(md);
    REQUIRE(waitFor([&]() { return app.getMatchingEngine().getMarketSnapshot("TEST") != nullptr; }));

    // 发送一个超出涨停价的限价单，触发风险拒绝（不进入撮合引擎，因此不会生成 orderID）
    FixMessage order;
    order.set(tags::MsgType, "D");
    order.set(tags::ClOrdID, "ORD-REJECT-001");
    order.set(tags::Symbol, "TEST");
    order.set(tags::Side, "1");
    order.set(tags::OrderQty, "1");
    order.set(tags::OrdType, "2");
    order.set(tags::Price, "300");

    app.fromApp(order, sid);

    // 订单应存在于 DB 且状态已更新为 REJECTED，同时 orderID 仍为空。
    REQUIRE(waitFor([&]() {
        auto loaded = store.loadOrder("ORD-REJECT-001");
        return loaded.has_value() && loaded->status == OrderStatus::REJECTED;
    }));

    auto loaded = store.loadOrder("ORD-REJECT-001");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->orderID.empty());

    app.stop();
}
