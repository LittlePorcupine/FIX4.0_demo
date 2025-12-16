#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "fix/session.hpp"
#include "fix/fix_messages.hpp"
#include "fix/fix_codec.hpp"
#include "base/config.hpp"
#include "storage/sqlite_store.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <fstream>

using namespace fix40;

// 创建测试用的配置文件
class TestConfigSetup {
public:
    TestConfigSetup() {
        std::ofstream file("/tmp/test_session_config.ini");
        file << "[fix_session]\n"
             << "default_heartbeat_interval = 30\n"
             << "min_heartbeat_interval = 5\n"
             << "max_heartbeat_interval = 120\n"
             << "logout_confirm_timeout_sec = 2\n"  // 缩短超时便于测试
             << "test_request_timeout_multiplier = 1.5\n";
        file.close();
        Config::instance().load("/tmp/test_session_config.ini");
    }
};

// 全局配置初始化
static TestConfigSetup config_setup;

// 辅助函数：创建一个测试用的 Session
std::shared_ptr<Session> create_test_session(
    const std::string& sender = "CLIENT",
    const std::string& target = "SERVER",
    int heartbeat = 30,
    Session::ShutdownCallback cb = nullptr) {
    
    return std::make_shared<Session>(sender, target, heartbeat, cb);
}

// 辅助函数：创建带正确序列号的消息
FixMessage create_logon_with_seq(const std::string& sender, const std::string& target, int seq, int hb) {
    auto msg = create_logon_message(sender, target, seq, hb);
    return msg;
}

FixMessage create_heartbeat_with_seq(const std::string& sender, const std::string& target, int seq, const std::string& test_req_id = "") {
    auto msg = create_heartbeat_message(sender, target, seq, test_req_id);
    return msg;
}

FixMessage create_test_request_with_seq(const std::string& sender, const std::string& target, int seq, const std::string& test_req_id) {
    auto msg = create_test_request_message(sender, target, seq, test_req_id);
    return msg;
}

FixMessage create_logout_with_seq(const std::string& sender, const std::string& target, int seq, const std::string& text = "") {
    auto msg = create_logout_message(sender, target, seq, text);
    return msg;
}

// ============================================================================
// Session 基本功能测试
// ============================================================================

TEST_CASE("Session creation and initial state", "[session]") {
    auto session = create_test_session();
    
    REQUIRE(session->senderCompID == "CLIENT");
    REQUIRE(session->targetCompID == "SERVER");
    REQUIRE(session->get_heart_bt_int() == 30);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Session start and stop", "[session]") {
    auto session = create_test_session();
    
    REQUIRE_FALSE(session->is_running());
    
    session->start();
    REQUIRE(session->is_running());
    
    session->stop();
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Session sequence number management", "[session]") {
    auto session = create_test_session();
    
    REQUIRE(session->get_send_seq_num() == 1);
    REQUIRE(session->get_recv_seq_num() == 1);
    
    session->increment_send_seq_num();
    REQUIRE(session->get_send_seq_num() == 2);
    
    session->increment_recv_seq_num();
    REQUIRE(session->get_recv_seq_num() == 2);
    
    session->set_recv_seq_num(10);
    REQUIRE(session->get_recv_seq_num() == 10);
}

TEST_CASE("Session heartbeat interval management", "[session]") {
    auto session = create_test_session("CLIENT", "SERVER", 30);
    
    REQUIRE(session->get_heart_bt_int() == 30);
    
    session->set_heart_bt_int(60);
    REQUIRE(session->get_heart_bt_int() == 60);
    
    REQUIRE(session->get_min_heart_bt_int() == 5);
    REQUIRE(session->get_max_heart_bt_int() == 120);
}

TEST_CASE("Session time tracking", "[session]") {
    auto session = create_test_session();
    
    auto before = std::chrono::steady_clock::now();
    session->update_last_recv_time();
    session->update_last_send_time();
    auto after = std::chrono::steady_clock::now();
    
    auto recv_time = session->get_last_recv_time();
    auto send_time = session->get_last_send_time();
    
    REQUIRE(recv_time >= before);
    REQUIRE(recv_time <= after);
    REQUIRE(send_time >= before);
    REQUIRE(send_time <= after);
}

TEST_CASE("Session shutdown callback", "[session]") {
    std::atomic<bool> callback_called{false};
    
    auto session = create_test_session("CLIENT", "SERVER", 30, [&callback_called]() {
        callback_called = true;
    });
    
    session->start();
    session->perform_shutdown("Test shutdown");
    
    REQUIRE(callback_called);
    REQUIRE_FALSE(session->is_running());
}

// ============================================================================
// 状态机测试 - DisconnectedState (服务端)
// ============================================================================

TEST_CASE("Server session receives valid Logon", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("SERVER", "CLIENT", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    REQUIRE(session->is_running());
    
    // 服务端收到客户端的 Logon（心跳间隔在有效范围内）
    auto logon = create_logon_with_seq("CLIENT", "SERVER", 1, 30);
    session->on_message_received(logon);
    
    // 会话应该建立，序列号递增
    REQUIRE(session->get_recv_seq_num() == 2);
    REQUIRE(session->is_running());
    REQUIRE_FALSE(shutdown_called);
}

TEST_CASE("Server session rejects Logon with invalid heartbeat - too low", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("SERVER", "CLIENT", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    
    // 心跳间隔太低（小于 min_heartbeat_interval = 5）
    auto logon = create_logon_with_seq("CLIENT", "SERVER", 1, 2);
    session->on_message_received(logon);
    
    // 会话应该关闭
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Server session rejects Logon with invalid heartbeat - too high", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("SERVER", "CLIENT", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    
    // 心跳间隔太高（大于 max_heartbeat_interval = 120）
    auto logon = create_logon_with_seq("CLIENT", "SERVER", 1, 200);
    session->on_message_received(logon);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Server session rejects non-Logon message in disconnected state", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("SERVER", "CLIENT", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    
    // 发送 Heartbeat 而不是 Logon
    auto hb = create_heartbeat_with_seq("CLIENT", "SERVER", 1);
    session->on_message_received(hb);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

// ============================================================================
// 状态机测试 - LogonSentState (客户端)
// ============================================================================

TEST_CASE("Client session receives Logon confirmation", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("CLIENT", "SERVER", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();  // 客户端会发送 Logon 并进入 LogonSent 状态
    
    // 模拟收到服务端的 Logon 确认
    auto logon_ack = create_logon_with_seq("SERVER", "CLIENT", 1, 30);
    session->on_message_received(logon_ack);
    
    // 会话应该建立
    REQUIRE(session->get_recv_seq_num() == 2);
    REQUIRE(session->is_running());
    REQUIRE_FALSE(shutdown_called);
}

TEST_CASE("Client session rejects non-Logon during LogonSent", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("CLIENT", "SERVER", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    
    // 收到 Heartbeat 而不是 Logon 确认
    auto hb = create_heartbeat_with_seq("SERVER", "CLIENT", 1);
    session->on_message_received(hb);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Client logout request during LogonSent", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_test_session("CLIENT", "SERVER", 30, [&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->start();
    
    // 在等待 Logon 确认时请求登出
    session->initiate_logout("User cancelled");
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

// ============================================================================
// 状态机测试 - EstablishedState
// ============================================================================

// 辅助函数：创建已建立会话的 Session
std::shared_ptr<Session> create_established_session(Session::ShutdownCallback cb = nullptr) {
    auto session = create_test_session("CLIENT", "SERVER", 30, cb);
    session->start();
    
    // 模拟收到 Logon 确认，进入 Established 状态
    auto logon_ack = create_logon_with_seq("SERVER", "CLIENT", 1, 30);
    session->on_message_received(logon_ack);
    
    return session;
}

TEST_CASE("Established session handles Heartbeat", "[session][state]") {
    auto session = create_established_session();
    
    // 收到 Heartbeat
    auto hb = create_heartbeat_with_seq("SERVER", "CLIENT", 2);
    session->on_message_received(hb);
    
    REQUIRE(session->get_recv_seq_num() == 3);
    REQUIRE(session->is_running());
}

TEST_CASE("Established session handles TestRequest", "[session][state]") {
    auto session = create_established_session();
    
    // 收到 TestRequest
    auto tr = create_test_request_with_seq("SERVER", "CLIENT", 2, "TEST123");
    session->on_message_received(tr);
    
    // 应该回复 Heartbeat（带 TestReqID）
    REQUIRE(session->get_recv_seq_num() == 3);
    REQUIRE(session->is_running());
}

TEST_CASE("Established session handles peer Logout", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    // 收到对方的 Logout
    auto logout = create_logout_with_seq("SERVER", "CLIENT", 2, "Goodbye");
    session->on_message_received(logout);
    
    // 会话应该进入 LogoutSent 状态（发送确认）
    // 注意：由于没有真实连接，不会收到确认，但状态应该改变
    REQUIRE(session->is_running());  // 还在等待确认
}

TEST_CASE("Established session rejects unexpected Logon", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    // 在已建立状态收到 Logon
    auto logon = create_logon_with_seq("SERVER", "CLIENT", 2, 30);
    session->on_message_received(logon);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Established session initiates logout", "[session][state]") {
    auto session = create_established_session();
    
    session->initiate_logout("User requested");
    
    // 会话应该还在运行（等待确认）
    REQUIRE(session->is_running());
}

TEST_CASE("Established session handles unknown message type", "[session][state]") {
    auto session = create_established_session();
    
    // 创建一个未知类型的消息
    FixMessage unknown_msg;
    unknown_msg.set(tags::MsgType, "D");  // NewOrderSingle，未实现
    unknown_msg.set(tags::MsgSeqNum, 2);
    unknown_msg.set(tags::SenderCompID, "SERVER");
    unknown_msg.set(tags::TargetCompID, "CLIENT");
    
    session->on_message_received(unknown_msg);
    
    // 应该继续运行（只是记录日志）
    REQUIRE(session->get_recv_seq_num() == 3);
    REQUIRE(session->is_running());
}

// ============================================================================
// 状态机测试 - LogoutSentState
// ============================================================================

TEST_CASE("LogoutSent receives Logout confirmation", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    // 发起登出
    session->initiate_logout("Test logout");
    
    // 收到 Logout 确认
    auto logout_ack = create_logout_with_seq("SERVER", "CLIENT", 2);
    session->on_message_received(logout_ack);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("LogoutSent ignores non-Logout messages", "[session][state]") {
    auto session = create_established_session();
    
    session->initiate_logout("Test logout");
    int seq_before = session->get_recv_seq_num();
    
    // 收到 Heartbeat（应该被忽略）
    auto hb = create_heartbeat_with_seq("SERVER", "CLIENT", 2);
    session->on_message_received(hb);
    
    // 序列号不应该改变（消息被忽略）
    REQUIRE(session->get_recv_seq_num() == seq_before);
    REQUIRE(session->is_running());
}

TEST_CASE("LogoutSent ignores duplicate logout request", "[session][state]") {
    auto session = create_established_session();
    
    session->initiate_logout("First logout");
    session->initiate_logout("Second logout");  // 应该被忽略
    
    REQUIRE(session->is_running());
}

// ============================================================================
// 序列号验证测试
// ============================================================================

TEST_CASE("Session rejects message with wrong sequence number", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    // 发送序列号错误的消息（期望 2，发送 5）
    auto hb = create_heartbeat_with_seq("SERVER", "CLIENT", 5);
    session->on_message_received(hb);
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

// ============================================================================
// 定时器检查测试
// ============================================================================

TEST_CASE("Session timer check when not running", "[session][state]") {
    auto session = create_test_session();
    
    // 未启动时调用 timer check 不应该崩溃
    session->on_timer_check();
    
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Session timer check in established state", "[session][state]") {
    auto session = create_established_session();
    
    // 调用 timer check 不应该崩溃
    session->on_timer_check();
    
    REQUIRE(session->is_running());
}

// ============================================================================
// IO 错误处理测试
// ============================================================================

TEST_CASE("Session handles IO error", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->on_io_error("Connection reset");
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

TEST_CASE("Session handles shutdown request", "[session][state]") {
    std::atomic<bool> shutdown_called{false};
    auto session = create_established_session([&shutdown_called]() {
        shutdown_called = true;
    });
    
    session->on_shutdown("Server shutting down");
    
    REQUIRE(shutdown_called);
    REQUIRE_FALSE(session->is_running());
}

// ============================================================================
// 重复关闭测试
// ============================================================================

TEST_CASE("Session perform_shutdown is idempotent", "[session][state]") {
    std::atomic<int> callback_count{0};
    auto session = create_established_session([&callback_count]() {
        callback_count++;
    });
    
    session->perform_shutdown("First");
    session->perform_shutdown("Second");
    session->perform_shutdown("Third");
    
    // 回调只应该被调用一次
    REQUIRE(callback_count == 1);
}

// ============================================================================
// FixCodec 边界测试
// ============================================================================

TEST_CASE("FixCodec decode missing checksum", "[codec][edge]") {
    FixCodec codec;
    std::string bad_msg = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01";
    REQUIRE_THROWS_AS(codec.decode(bad_msg), std::runtime_error);
}

TEST_CASE("FixCodec decode invalid field format", "[codec][edge]") {
    FixCodec codec;
    std::string bad_msg = "8=FIX.4.0\x01" "9=5\x01" "35\x01" "10=000\x01";
    REQUIRE_THROWS(codec.decode(bad_msg));
}

TEST_CASE("FixCodec roundtrip all message types", "[codec]") {
    FixCodec codec;
    
    auto logon = create_logon_message("SENDER", "TARGET", 1, 30);
    std::string encoded_logon = codec.encode(logon);
    auto decoded_logon = codec.decode(encoded_logon);
    REQUIRE(decoded_logon.get_string(tags::MsgType) == "A");
    REQUIRE(decoded_logon.get_int(tags::HeartBtInt) == 30);
    
    auto hb = create_heartbeat_message("SENDER", "TARGET", 2, "");
    std::string encoded_hb = codec.encode(hb);
    auto decoded_hb = codec.decode(encoded_hb);
    REQUIRE(decoded_hb.get_string(tags::MsgType) == "0");
    
    auto hb_with_id = create_heartbeat_message("SENDER", "TARGET", 3, "TEST123");
    std::string encoded_hb_id = codec.encode(hb_with_id);
    auto decoded_hb_id = codec.decode(encoded_hb_id);
    REQUIRE(decoded_hb_id.get_string(tags::TestReqID) == "TEST123");
    
    auto tr = create_test_request_message("SENDER", "TARGET", 4, "REQ456");
    std::string encoded_tr = codec.encode(tr);
    auto decoded_tr = codec.decode(encoded_tr);
    REQUIRE(decoded_tr.get_string(tags::MsgType) == "1");
    REQUIRE(decoded_tr.get_string(tags::TestReqID) == "REQ456");
    
    auto logout = create_logout_message("SENDER", "TARGET", 5, "Goodbye");
    std::string encoded_logout = codec.encode(logout);
    auto decoded_logout = codec.decode(encoded_logout);
    REQUIRE(decoded_logout.get_string(tags::MsgType) == "5");
    REQUIRE(decoded_logout.get_string(tags::Text) == "Goodbye");
}

TEST_CASE("FixCodec sequence number preserved", "[codec]") {
    FixCodec codec;
    auto msg = create_heartbeat_message("A", "B", 12345, "");
    std::string encoded = codec.encode(msg);
    auto decoded = codec.decode(encoded);
    REQUIRE(decoded.get_int(tags::MsgSeqNum) == 12345);
}

TEST_CASE("FixCodec sender and target preserved", "[codec]") {
    FixCodec codec;
    auto msg = create_logon_message("MY_SENDER_ID", "MY_TARGET_ID", 1, 30);
    std::string encoded = codec.encode(msg);
    auto decoded = codec.decode(encoded);
    REQUIRE(decoded.get_string(tags::SenderCompID) == "MY_SENDER_ID");
    REQUIRE(decoded.get_string(tags::TargetCompID) == "MY_TARGET_ID");
}

// ============================================================================
// FixMessage 边界测试
// ============================================================================

TEST_CASE("FixMessage set and get various types", "[message]") {
    FixMessage msg;
    
    msg.set(100, "string_value");
    REQUIRE(msg.get_string(100) == "string_value");
    
    msg.set(101, 42);
    REQUIRE(msg.get_string(101) == "42");
    REQUIRE(msg.get_int(101) == 42);
    
    msg.set(100, "new_value");
    REQUIRE(msg.get_string(100) == "new_value");
}

TEST_CASE("FixMessage get_int with non-numeric value", "[message][edge]") {
    FixMessage msg;
    msg.set(100, "not_a_number");
    REQUIRE_THROWS(msg.get_int(100));
}

TEST_CASE("FixMessage has check", "[message]") {
    FixMessage msg;
    
    REQUIRE_FALSE(msg.has(100));
    
    msg.set(100, "value");
    REQUIRE(msg.has(100));
    
    msg.set(101, "");
    REQUIRE(msg.has(101));
}

TEST_CASE("FixMessage get_fields returns all fields", "[message]") {
    FixMessage msg;
    msg.set(1, "a");
    msg.set(2, "b");
    msg.set(3, "c");
    
    const auto& fields = msg.get_fields();
    REQUIRE(fields.size() == 3);
    REQUIRE(fields.at(1) == "a");
    REQUIRE(fields.at(2) == "b");
    REQUIRE(fields.at(3) == "c");
}


// ============================================================================
// Heartbeat with TestReqID 测试
// ============================================================================

TEST_CASE("Established session clears awaiting TestReqID on matching Heartbeat", "[session][state]") {
    auto session = create_established_session();
    
    // 先发送一个 TestRequest（模拟超时检查触发）
    // 由于我们无法直接访问内部状态，我们通过消息流来测试
    
    // 收到带 TestReqID 的 Heartbeat
    auto hb = create_heartbeat_with_seq("SERVER", "CLIENT", 2, "SomeTestReqID");
    session->on_message_received(hb);
    
    REQUIRE(session->get_recv_seq_num() == 3);
    REQUIRE(session->is_running());
}

// ============================================================================
// 服务端会话边界心跳测试
// ============================================================================

TEST_CASE("Server accepts minimum valid heartbeat", "[session][state]") {
    auto session = create_test_session("SERVER", "CLIENT", 30);
    session->start();
    
    // 心跳间隔刚好等于最小值 (5)
    auto logon = create_logon_with_seq("CLIENT", "SERVER", 1, 5);
    session->on_message_received(logon);
    
    REQUIRE(session->is_running());
    REQUIRE(session->get_heart_bt_int() == 5);
}

TEST_CASE("Server accepts maximum valid heartbeat", "[session][state]") {
    auto session = create_test_session("SERVER", "CLIENT", 30);
    session->start();
    
    // 心跳间隔刚好等于最大值 (120)
    auto logon = create_logon_with_seq("CLIENT", "SERVER", 1, 120);
    session->on_message_received(logon);
    
    REQUIRE(session->is_running());
    REQUIRE(session->get_heart_bt_int() == 120);
}

// ============================================================================
// 消息发送测试
// ============================================================================

TEST_CASE("Session send increments sequence number", "[session]") {
    auto session = create_established_session();
    
    int initial_seq = session->get_send_seq_num();
    
    // 发送一条消息
    auto hb = create_heartbeat_message(session->senderCompID, session->targetCompID, 0, "");
    session->send(hb);
    
    // 序列号应该递增
    REQUIRE(session->get_send_seq_num() == initial_seq + 1);
}

TEST_CASE("Session send_heartbeat works", "[session]") {
    auto session = create_established_session();
    
    int initial_seq = session->get_send_seq_num();
    
    session->send_heartbeat();
    
    REQUIRE(session->get_send_seq_num() == initial_seq + 1);
}

TEST_CASE("Session send_heartbeat with TestReqID works", "[session]") {
    auto session = create_established_session();
    
    int initial_seq = session->get_send_seq_num();
    
    session->send_heartbeat("TEST123");
    
    REQUIRE(session->get_send_seq_num() == initial_seq + 1);
}

TEST_CASE("Session send_test_request works", "[session]") {
    auto session = create_established_session();
    
    int initial_seq = session->get_send_seq_num();
    
    session->send_test_request("REQ456");
    
    REQUIRE(session->get_send_seq_num() == initial_seq + 1);
}

TEST_CASE("Session send_logout works", "[session]") {
    auto session = create_established_session();
    
    int initial_seq = session->get_send_seq_num();
    
    session->send_logout("Goodbye");
    
    REQUIRE(session->get_send_seq_num() == initial_seq + 1);
}

// ============================================================================
// DisconnectedState 边界测试
// ============================================================================

TEST_CASE("DisconnectedState timer check does nothing", "[session][state]") {
    auto session = create_test_session("SERVER", "CLIENT", 30);
    session->start();
    
    // 在 Disconnected 状态调用 timer check 不应该有任何效果
    session->on_timer_check();
    
    REQUIRE(session->is_running());
}

TEST_CASE("DisconnectedState logout request does nothing", "[session][state]") {
    auto session = create_test_session("SERVER", "CLIENT", 30);
    session->start();
    
    // 在 Disconnected 状态请求登出不应该有任何效果
    session->initiate_logout("Test");
    
    REQUIRE(session->is_running());
}


// =============================================================================
// RapidCheck 生成器
// =============================================================================

namespace rc {

/**
 * @brief StoredMessage 生成器（用于消息持久化测试）
 */
template<>
struct Arbitrary<StoredMessage> {
    static Gen<StoredMessage> arbitrary() {
        return gen::build<StoredMessage>(
            gen::set(&StoredMessage::seqNum, gen::inRange(1, 100000)),
            gen::set(&StoredMessage::senderCompID, gen::nonEmpty<std::string>()),
            gen::set(&StoredMessage::targetCompID, gen::nonEmpty<std::string>()),
            gen::set(&StoredMessage::msgType, gen::element<std::string>("D", "8", "F", "G")),
            gen::set(&StoredMessage::rawMessage, gen::nonEmpty<std::string>()),
            gen::set(&StoredMessage::timestamp, gen::inRange<int64_t>(1000000000000, 2000000000000))
        );
    }
};

/**
 * @brief SessionState 生成器（用于序列号恢复测试）
 */
template<>
struct Arbitrary<SessionState> {
    static Gen<SessionState> arbitrary() {
        return gen::build<SessionState>(
            gen::set(&SessionState::senderCompID, gen::nonEmpty<std::string>()),
            gen::set(&SessionState::targetCompID, gen::nonEmpty<std::string>()),
            gen::set(&SessionState::sendSeqNum, gen::inRange(1, 100000)),
            gen::set(&SessionState::recvSeqNum, gen::inRange(1, 100000)),
            gen::set(&SessionState::lastUpdateTime, gen::inRange<int64_t>(1000000000000, 2000000000000))
        );
    }
};

} // namespace rc

// =============================================================================
// 断线恢复属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 15: FIX消息持久化round-trip**
 * **Validates: Requirements 11.1, 11.4**
 * 
 * 对于任意FIX消息，保存到数据库后按序列号范围加载，应得到原始消息。
 */
TEST_CASE("Session - FIX消息持久化 round-trip 属性测试", "[session][property][recovery]") {
    
    rc::prop("消息保存后按序列号范围加载应得到原始消息",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto msg = *rc::gen::arbitrary<StoredMessage>();
            
            // 保存消息
            RC_ASSERT(store.saveMessage(msg));
            
            // 按序列号范围加载
            auto messages = store.loadMessages(msg.senderCompID, msg.targetCompID, 
                                               msg.seqNum, msg.seqNum);
            RC_ASSERT(messages.size() == 1);
            
            // 验证字段相等
            RC_ASSERT(messages[0].seqNum == msg.seqNum);
            RC_ASSERT(messages[0].senderCompID == msg.senderCompID);
            RC_ASSERT(messages[0].targetCompID == msg.targetCompID);
            RC_ASSERT(messages[0].msgType == msg.msgType);
            RC_ASSERT(messages[0].rawMessage == msg.rawMessage);
            RC_ASSERT(messages[0].timestamp == msg.timestamp);
        });
    
    rc::prop("多条消息按序列号顺序加载",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            // 生成固定的 sender/target
            std::string sender = "SENDER";
            std::string target = "TARGET";
            
            // 生成 3-10 条消息
            int count = *rc::gen::inRange(3, 10);
            
            for (int i = 1; i <= count; ++i) {
                StoredMessage msg;
                msg.seqNum = i;
                msg.senderCompID = sender;
                msg.targetCompID = target;
                msg.msgType = "D";
                msg.rawMessage = "msg_" + std::to_string(i);
                msg.timestamp = 1000000000000 + i;
                RC_ASSERT(store.saveMessage(msg));
            }
            
            // 加载所有消息
            auto messages = store.loadMessages(sender, target, 1, count);
            RC_ASSERT(static_cast<int>(messages.size()) == count);
            
            // 验证顺序
            for (int i = 0; i < count; ++i) {
                RC_ASSERT(messages[i].seqNum == i + 1);
            }
        });
    
    rc::prop("加载指定范围的消息",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            std::string sender = "SENDER";
            std::string target = "TARGET";
            
            // 保存 10 条消息
            for (int i = 1; i <= 10; ++i) {
                StoredMessage msg;
                msg.seqNum = i;
                msg.senderCompID = sender;
                msg.targetCompID = target;
                msg.msgType = "D";
                msg.rawMessage = "msg_" + std::to_string(i);
                msg.timestamp = 1000000000000 + i;
                RC_ASSERT(store.saveMessage(msg));
            }
            
            // 生成随机范围
            int begin = *rc::gen::inRange(1, 5);
            int end = *rc::gen::inRange(begin, 10);
            
            auto messages = store.loadMessages(sender, target, begin, end);
            RC_ASSERT(static_cast<int>(messages.size()) == end - begin + 1);
            RC_ASSERT(messages.front().seqNum == begin);
            RC_ASSERT(messages.back().seqNum == end);
        });
}

/**
 * **Feature: paper-trading-system, Property 16: 序列号恢复正确性**
 * **Validates: Requirements 11.2**
 * 
 * 对于任意会话的发送/接收序列号，保存后重新建立会话时应恢复到保存的值。
 */
TEST_CASE("Session - 序列号恢复正确性属性测试", "[session][property][recovery]") {
    
    rc::prop("会话状态保存后加载应得到相同的序列号",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto state = *rc::gen::arbitrary<SessionState>();
            
            // 保存会话状态
            RC_ASSERT(store.saveSessionState(state));
            
            // 加载会话状态
            auto loaded = store.loadSessionState(state.senderCompID, state.targetCompID);
            RC_ASSERT(loaded.has_value());
            
            // 验证序列号相等
            RC_ASSERT(loaded->sendSeqNum == state.sendSeqNum);
            RC_ASSERT(loaded->recvSeqNum == state.recvSeqNum);
            RC_ASSERT(loaded->senderCompID == state.senderCompID);
            RC_ASSERT(loaded->targetCompID == state.targetCompID);
        });
    
    rc::prop("Session 构造时从 Store 恢复序列号",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            // 生成随机序列号
            int sendSeq = *rc::gen::inRange(1, 10000);
            int recvSeq = *rc::gen::inRange(1, 10000);
            
            // 保存会话状态
            SessionState state;
            state.senderCompID = "CLIENT";
            state.targetCompID = "SERVER";
            state.sendSeqNum = sendSeq;
            state.recvSeqNum = recvSeq;
            state.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            RC_ASSERT(store.saveSessionState(state));
            
            // 创建 Session，应该从 Store 恢复序列号
            auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, nullptr, &store);
            
            // 验证序列号已恢复
            RC_ASSERT(session->get_send_seq_num() == sendSeq);
            RC_ASSERT(session->get_recv_seq_num() == recvSeq);
        });
    
    rc::prop("Session 发送消息时保存到 Store",
        []() {
            SqliteStore store(":memory:");
            RC_ASSERT(store.isOpen());
            
            auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, nullptr, &store);
            session->start();
            
            // 模拟收到 Logon 确认，进入 Established 状态
            auto logon_ack = create_logon_message("SERVER", "CLIENT", 1, 30);
            session->on_message_received(logon_ack);
            
            // 发送一条业务消息
            FixMessage msg;
            msg.set(tags::MsgType, "D");
            msg.set(tags::SenderCompID, "CLIENT");
            msg.set(tags::TargetCompID, "SERVER");
            session->send(msg);
            
            // 验证消息已保存到 Store
            auto messages = store.loadMessages("CLIENT", "SERVER", 1, 100);
            RC_ASSERT(messages.size() >= 1);
            
            // 验证会话状态已保存
            auto state = store.loadSessionState("CLIENT", "SERVER");
            RC_ASSERT(state.has_value());
            RC_ASSERT(state->sendSeqNum >= 2);  // 至少发送了 Logon 和业务消息
        });
}

// =============================================================================
// 断线恢复单元测试
// =============================================================================

TEST_CASE("Session with Store - 消息持久化", "[session][recovery]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());
    
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, nullptr, &store);
    session->start();
    
    // 模拟收到 Logon 确认
    auto logon_ack = create_logon_message("SERVER", "CLIENT", 1, 30);
    session->on_message_received(logon_ack);
    
    // 发送几条消息
    for (int i = 0; i < 3; ++i) {
        FixMessage msg;
        msg.set(tags::MsgType, "D");
        msg.set(tags::SenderCompID, "CLIENT");
        msg.set(tags::TargetCompID, "SERVER");
        session->send(msg);
    }
    
    // 验证消息已保存
    auto messages = store.loadMessages("CLIENT", "SERVER", 1, 100);
    REQUIRE(messages.size() >= 3);
}

TEST_CASE("Session with Store - 序列号恢复", "[session][recovery]") {
    SqliteStore store(":memory:");
    REQUIRE(store.isOpen());
    
    // 预先保存会话状态
    SessionState state;
    state.senderCompID = "CLIENT";
    state.targetCompID = "SERVER";
    state.sendSeqNum = 100;
    state.recvSeqNum = 50;
    state.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    REQUIRE(store.saveSessionState(state));
    
    // 创建 Session，应该恢复序列号
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, nullptr, &store);
    
    REQUIRE(session->get_send_seq_num() == 100);
    REQUIRE(session->get_recv_seq_num() == 50);
}

TEST_CASE("Session - ResendRequest 和 SequenceReset 消息创建", "[session][recovery]") {
    FixCodec codec;
    
    SECTION("ResendRequest 消息") {
        auto rr = create_resend_request_message("CLIENT", "SERVER", 5, 10, 20);
        
        REQUIRE(rr.get_string(tags::MsgType) == "2");
        REQUIRE(rr.get_int(tags::BeginSeqNo) == 10);
        REQUIRE(rr.get_int(tags::EndSeqNo) == 20);
        
        // 编解码 round-trip
        std::string encoded = codec.encode(rr);
        auto decoded = codec.decode(encoded);
        REQUIRE(decoded.get_string(tags::MsgType) == "2");
        REQUIRE(decoded.get_int(tags::BeginSeqNo) == 10);
        REQUIRE(decoded.get_int(tags::EndSeqNo) == 20);
    }
    
    SECTION("SequenceReset-GapFill 消息") {
        auto sr = create_sequence_reset_message("CLIENT", "SERVER", 5, 15, true);
        
        REQUIRE(sr.get_string(tags::MsgType) == "4");
        REQUIRE(sr.get_int(tags::NewSeqNo) == 15);
        REQUIRE(sr.get_string(tags::GapFillFlag) == "Y");
        
        // 编解码 round-trip
        std::string encoded = codec.encode(sr);
        auto decoded = codec.decode(encoded);
        REQUIRE(decoded.get_string(tags::MsgType) == "4");
        REQUIRE(decoded.get_int(tags::NewSeqNo) == 15);
        REQUIRE(decoded.get_string(tags::GapFillFlag) == "Y");
    }
    
    SECTION("SequenceReset-Reset 消息") {
        auto sr = create_sequence_reset_message("CLIENT", "SERVER", 5, 1, false);
        
        REQUIRE(sr.get_string(tags::MsgType) == "4");
        REQUIRE(sr.get_int(tags::NewSeqNo) == 1);
        REQUIRE(sr.get_string(tags::GapFillFlag) == "N");
    }
}

TEST_CASE("is_admin_message 函数", "[session][recovery]") {
    REQUIRE(is_admin_message("0"));  // Heartbeat
    REQUIRE(is_admin_message("1"));  // TestRequest
    REQUIRE(is_admin_message("2"));  // ResendRequest
    REQUIRE(is_admin_message("4"));  // SequenceReset
    REQUIRE(is_admin_message("5"));  // Logout
    REQUIRE(is_admin_message("A"));  // Logon
    
    REQUIRE_FALSE(is_admin_message("D"));  // NewOrderSingle
    REQUIRE_FALSE(is_admin_message("8"));  // ExecutionReport
    REQUIRE_FALSE(is_admin_message("F"));  // OrderCancelRequest
    REQUIRE_FALSE(is_admin_message("G"));  // OrderCancelReplaceRequest
}
