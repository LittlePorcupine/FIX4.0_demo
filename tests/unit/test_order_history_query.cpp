#include "../catch2/catch.hpp"
#include "app/simulation_app.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "storage/sqlite_store.hpp"

using namespace fix40;

TEST_CASE("SimulationApp - order history query (U9/U10)", "[application][storage]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SimulationApp app(&store);

    // 准备一个可用于 extractAccountId 的会话
    auto session = std::make_shared<Session>("SERVER", "CLIENT1", 30, nullptr, &store);
    session->set_client_comp_id("CLIENT1");
    app.getSessionManager().registerSession(session);
    SessionID sid = session->get_session_id();
    session->start();

    // 写入两笔订单：一笔属于 CLIENT1，一笔属于 CLIENT2（应被过滤）
    Order o1;
    o1.clOrdID = "CLIENT1-000001";
    o1.orderID = "ORD-0000000001";
    o1.symbol = "IF2601";
    o1.side = OrderSide::BUY;
    o1.ordType = OrderType::LIMIT;
    o1.timeInForce = TimeInForce::DAY;
    o1.price = 4500.0;
    o1.orderQty = 2;
    o1.cumQty = 1;
    o1.leavesQty = 1;
    o1.avgPx = 4499.5;
    o1.status = OrderStatus::PARTIALLY_FILLED;
    REQUIRE(store.saveOrder(o1));

    Order o2 = o1;
    o2.clOrdID = "CLIENT2-000001";
    o2.orderID = "ORD-0000000002";
    REQUIRE(store.saveOrder(o2));

    // 发起订单历史查询
    FixMessage req;
    req.set(tags::MsgType, "U9");
    req.set(tags::RequestID, "REQ-1");
    app.fromApp(req, sid);

    // 响应消息应通过 Session::send 持久化到 store.messages
    auto messages = store.loadMessages("SERVER", "CLIENT1", 1, 100);
    REQUIRE_FALSE(messages.empty());

    FixCodec codec;
    bool found = false;
    for (const auto& m : messages) {
        FixMessage decoded = codec.decode(m.rawMessage);
        if (decoded.get_string(tags::MsgType) != "U10") {
            continue;
        }
        found = true;
        REQUIRE(decoded.get_string(tags::RequestID) == "REQ-1");
        REQUIRE(decoded.has(tags::Text));

        const std::string payload = decoded.get_string(tags::Text);
        REQUIRE(payload.find("CLIENT1-000001|") != std::string::npos);
        REQUIRE(payload.find("CLIENT2-000001|") == std::string::npos);
    }

    REQUIRE(found);
}
