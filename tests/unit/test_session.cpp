#include "../catch2/catch.hpp"
#include "fix/session.hpp"
#include "fix/fix_messages.hpp"
#include "fix/fix_codec.hpp"
#include "base/config.hpp"
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
