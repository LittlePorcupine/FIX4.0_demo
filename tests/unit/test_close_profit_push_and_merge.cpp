#include "../catch2/catch.hpp"

#include "app/simulation_app.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "storage/sqlite_store.hpp"

#include "client_app.hpp"
#include "client_state.hpp"

using namespace fix40;

namespace fix40 {
struct SimulationAppTestAccess {
    static void pushAccountUpdate(SimulationApp& app, const std::string& userId, int reason) {
        app.pushAccountUpdate(userId, reason);
    }
};
} // namespace fix40

TEST_CASE("Server U5 push includes CloseProfit and client merges missing fields", "[account][close_profit]") {
    // --------------------------
    // 1) Server: U5 包含 CloseProfit
    // --------------------------
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    SimulationApp serverApp(&store);

    auto serverSession = std::make_shared<Session>("SERVER", "CLIENT1", 30, nullptr, &store);
    serverSession->set_client_comp_id("CLIENT1");
    serverApp.getSessionManager().registerSession(serverSession);
    serverSession->start();

    serverApp.getAccountManager().createAccount("CLIENT1", 1000000.0);
    REQUIRE(serverApp.getAccountManager().addCloseProfit("CLIENT1", 123.45));

    SimulationAppTestAccess::pushAccountUpdate(serverApp, "CLIENT1", 2);

    auto messages = store.loadMessages("SERVER", "CLIENT1", 1, 100);
    REQUIRE_FALSE(messages.empty());

    FixCodec codec;
    bool foundU5 = false;
    for (const auto& m : messages) {
        FixMessage decoded = codec.decode(m.rawMessage);
        if (decoded.get_string(tags::MsgType) != "U5") {
            continue;
        }
        foundU5 = true;
        REQUIRE(decoded.has(tags::CloseProfit));
        REQUIRE(decoded.get_string(tags::CloseProfit) == "123.45");
        REQUIRE(decoded.has(tags::RiskRatio));
        break;
    }
    REQUIRE(foundU5);

    // --------------------------
    // 2) Client: U5 缺失 CloseProfit 不应覆盖为 0
    // --------------------------
    auto clientState = std::make_shared<client::ClientState>();
    client::ClientApp clientApp(clientState, "CLIENT1");

    client::AccountInfo baseline;
    baseline.closeProfit = 999.0;
    baseline.balance = 100.0;
    clientState->updateAccount(baseline);

    FixMessage partialU5;
    partialU5.set(tags::MsgType, "U5");
    partialU5.set(tags::Balance, "200.00");
    // 故意不带 CloseProfit

    clientApp.fromApp(partialU5, SessionID("SERVER", "CLIENT1"));

    client::AccountInfo after = clientState->getAccount();
    REQUIRE(after.balance == Approx(200.0));
    REQUIRE(after.closeProfit == Approx(999.0));
}
