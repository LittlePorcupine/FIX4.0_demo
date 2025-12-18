#include "../catch2/catch.hpp"
#include "fix/session.hpp"
#include "fix/fix_tags.hpp"
#include "storage/sqlite_store.hpp"
#include <chrono>

using namespace fix40;

TEST_CASE("Server Session - restore seq after Logon when created with placeholder TargetCompID", "[session][recovery]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());

    // 预先保存会话状态：SERVER <-> CLIENT1
    SessionState state;
    state.senderCompID = "SERVER";
    state.targetCompID = "CLIENT1";
    state.sendSeqNum = 100;
    state.recvSeqNum = 50;
    state.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    REQUIRE(store.saveSessionState(state));

    // 服务端 accept 阶段：TargetCompID 未知，用占位符创建
    auto session = std::make_shared<Session>("SERVER", "PENDING", 30, nullptr, &store);
    session->start();

    // 客户端发来 Logon（重连场景：MsgSeqNum 继续上次会话）
    FixMessage logon;
    logon.set(tags::MsgType, "A");
    logon.set(tags::SenderCompID, "CLIENT1");
    logon.set(tags::TargetCompID, "SERVER");
    logon.set(tags::MsgSeqNum, 50);
    logon.set(tags::HeartBtInt, 30);
    logon.set(tags::EncryptMethod, "0");

    session->on_message_received(logon);

    // 目标 CompID 已绑定到真实客户端
    REQUIRE(session->get_session_id().targetCompID == "CLIENT1");

    // 序列号已按 store 恢复，并消耗了当前 Logon（recv = 51），发送了 LogonAck（send 从 100 -> 101）
    REQUIRE(session->get_recv_seq_num() == 51);
    REQUIRE(session->get_send_seq_num() == 101);

    // store 中的会话状态也应被更新
    auto loaded = store.loadSessionState("SERVER", "CLIENT1");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->recvSeqNum == 51);
    REQUIRE(loaded->sendSeqNum == 101);
}

