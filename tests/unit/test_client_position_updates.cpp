#include "../catch2/catch.hpp"

#include "client_state.hpp"

using namespace fix40::client;

TEST_CASE("ClientState - U6 with zero quantities clears position", "[client_state][position]") {
    ClientState state;

    PositionInfo seed;
    seed.instrumentId = "IF2601";
    seed.longPosition = 2;
    seed.longAvgPrice = 4000.0;
    seed.shortPosition = 0;
    seed.shortAvgPrice = 0.0;
    seed.profit = 123.0;
    seed.quantitiesValid = true;
    state.setPositions({seed});

    PositionInfo clear;
    clear.instrumentId = "IF2601";
    clear.longPosition = 0;
    clear.shortPosition = 0;
    clear.profit = 0.0;
    clear.quantitiesValid = true;
    state.updatePosition(clear);

    REQUIRE(state.getPositions().empty());
}

TEST_CASE("ClientState - position update without quantities does not clear", "[client_state][position]") {
    ClientState state;

    PositionInfo seed;
    seed.instrumentId = "IF2601";
    seed.longPosition = 2;
    seed.longAvgPrice = 4000.0;
    seed.shortPosition = 0;
    seed.shortAvgPrice = 0.0;
    seed.profit = 123.0;
    seed.quantitiesValid = true;
    state.setPositions({seed});

    // 模拟服务端推送缺少数量字段的情况：不应把持仓清空。
    PositionInfo partial;
    partial.instrumentId = "IF2601";
    partial.profit = 0.0;
    partial.quantitiesValid = false;
    state.updatePosition(partial);

    auto positions = state.getPositions();
    REQUIRE(positions.size() == 1);
    REQUIRE(positions[0].instrumentId == "IF2601");
    REQUIRE(positions[0].longPosition == 2);
}

