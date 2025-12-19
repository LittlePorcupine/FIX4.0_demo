#include "../catch2/catch.hpp"
#include "fix/session.hpp"
#include "fix/fix_messages.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "storage/sqlite_store.hpp"
#include <chrono>
 
using namespace fix40;
 
TEST_CASE("Session - ResetSeqNumFlag on Logon clears stored messages and resets seq", "[session][recovery]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());
 
    // 预置旧会话状态与旧消息（模拟服务端历史会话）
    SessionState state;
    state.senderCompID = "SERVER";
    state.targetCompID = "USER001";
    state.sendSeqNum = 20;
    state.recvSeqNum = 2;
    state.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    REQUIRE(store.saveSessionState(state));
 
    StoredMessage oldMsg;
    oldMsg.seqNum = 20;
    oldMsg.senderCompID = "SERVER";
    oldMsg.targetCompID = "USER001";
    oldMsg.msgType = "0";
    oldMsg.rawMessage = "8=FIX.4.0\0019=5\00135=0\00110=000\001";
    oldMsg.timestamp = state.lastUpdateTime;
    REQUIRE(store.saveMessage(oldMsg));
 
    // 服务端 accept 阶段使用占位符创建
    auto session = std::make_shared<Session>("SERVER", "PENDING", 30, nullptr, &store);
    session->start();
 
    // 客户端 Logon 请求重置序列号
    FixMessage logon = create_logon_message("USER001", "SERVER", 1, 30, true);
    session->on_message_received(logon);
 
    // 目标 CompID 已绑定
    REQUIRE(session->get_session_id().targetCompID == "USER001");
 
    // 发送序列号应从 1 开始发出 LogonAck，发送后变为 2
    REQUIRE(session->get_send_seq_num() == 2);
 
    // old message 应被清理，不再能查询到 seq 20
    auto messages = store.loadMessages("SERVER", "USER001", 1, 100);
    REQUIRE_FALSE(messages.empty());
    for (const auto& m : messages) {
        REQUIRE(m.seqNum != 20);
    }
 
    // 第一条应是 LogonAck（MsgType=A, SeqNum=1）
    FixCodec codec;
    FixMessage decoded = codec.decode(messages.front().rawMessage);
    REQUIRE(decoded.get_string(tags::MsgType) == "A");
    REQUIRE(decoded.get_int(tags::MsgSeqNum) == 1);
    REQUIRE(decoded.has(tags::ResetSeqNumFlag));
    REQUIRE(decoded.get_string(tags::ResetSeqNumFlag) == "Y");
}

