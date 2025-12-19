#include "../catch2/catch.hpp"
#include "client_app.hpp"
#include "client_state.hpp"
#include "fix/session.hpp"

using namespace fix40;
using namespace fix40::client;

TEST_CASE("ClientApp - sendNewOrder appends orders instead of overwriting", "[client][orders]") {
    auto state = std::make_shared<ClientState>();
    auto app = std::make_shared<ClientApp>(state, "USER001");

    // 使用真实 Session，但不需要网络连接：send_app_message 会正常走 send() 并更新状态。
    auto session = std::make_shared<Session>("USER001", "SERVER", 30, []() {});
    session->set_application(app.get());
    app->setSession(session);

    const auto id1 = app->sendNewOrder("IF2601", "1", 1, 4000.0, "2");
    const auto id2 = app->sendNewOrder("IF2601", "1", 1, 4001.0, "2");

    REQUIRE_FALSE(id1.empty());
    REQUIRE_FALSE(id2.empty());
    REQUIRE(id1 != id2);

    auto orders = state->getOrders();
    REQUIRE(orders.size() == 2);
    REQUIRE(orders[0].clOrdID == id1);
    REQUIRE(orders[1].clOrdID == id2);
}

