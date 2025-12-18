#include "../catch2/catch.hpp"
#include "app/simulation_app.hpp"
#include "fix/fix_tags.hpp"
#include "market/market_data.hpp"
#include "storage/sqlite_store.hpp"
#include <chrono>
#include <thread>

using namespace fix40;

namespace {

template<typename Predicate>
bool waitFor(Predicate pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

