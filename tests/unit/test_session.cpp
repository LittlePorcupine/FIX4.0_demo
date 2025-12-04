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
             << "logout_confirm_timeout_sec = 10\n"
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
    
    // 初始序列号
    REQUIRE(session->get_send_seq_num() == 1);
    REQUIRE(session->get_recv_seq_num() == 1);
    
    // 递增发送序列号
    session->increment_send_seq_num();
    REQUIRE(session->get_send_seq_num() == 2);
    
    // 递增接收序列号
    session->increment_recv_seq_num();
    REQUIRE(session->get_recv_seq_num() == 2);
    
    // 设置接收序列号
    session->set_recv_seq_num(10);
    REQUIRE(session->get_recv_seq_num() == 10);
}

TEST_CASE("Session heartbeat interval management", "[session]") {
    auto session = create_test_session("CLIENT", "SERVER", 30);
    
    REQUIRE(session->get_heart_bt_int() == 30);
    
    session->set_heart_bt_int(60);
    REQUIRE(session->get_heart_bt_int() == 60);
    
    // 检查最小/最大心跳间隔
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
// FixCodec 边界测试（补充现有测试）
// ============================================================================

TEST_CASE("FixCodec decode missing checksum", "[codec][edge]") {
    FixCodec codec;
    
    // 缺少 checksum 的消息
    std::string bad_msg = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01";
    
    REQUIRE_THROWS_AS(codec.decode(bad_msg), std::runtime_error);
}

TEST_CASE("FixCodec decode invalid field format", "[codec][edge]") {
    FixCodec codec;
    
    // 字段格式错误（缺少等号）
    std::string bad_msg = "8=FIX.4.0\x01" "9=5\x01" "35\x01" "10=000\x01";
    
    REQUIRE_THROWS(codec.decode(bad_msg));
}

TEST_CASE("FixCodec roundtrip all message types", "[codec]") {
    FixCodec codec;
    
    // Logon
    auto logon = create_logon_message("SENDER", "TARGET", 1, 30);
    std::string encoded_logon = codec.encode(logon);
    auto decoded_logon = codec.decode(encoded_logon);
    REQUIRE(decoded_logon.get_string(tags::MsgType) == "A");
    REQUIRE(decoded_logon.get_int(tags::HeartBtInt) == 30);
    
    // Heartbeat
    auto hb = create_heartbeat_message("SENDER", "TARGET", 2, "");
    std::string encoded_hb = codec.encode(hb);
    auto decoded_hb = codec.decode(encoded_hb);
    REQUIRE(decoded_hb.get_string(tags::MsgType) == "0");
    
    // Heartbeat with TestReqID
    auto hb_with_id = create_heartbeat_message("SENDER", "TARGET", 3, "TEST123");
    std::string encoded_hb_id = codec.encode(hb_with_id);
    auto decoded_hb_id = codec.decode(encoded_hb_id);
    REQUIRE(decoded_hb_id.get_string(tags::TestReqID) == "TEST123");
    
    // TestRequest
    auto tr = create_test_request_message("SENDER", "TARGET", 4, "REQ456");
    std::string encoded_tr = codec.encode(tr);
    auto decoded_tr = codec.decode(encoded_tr);
    REQUIRE(decoded_tr.get_string(tags::MsgType) == "1");
    REQUIRE(decoded_tr.get_string(tags::TestReqID) == "REQ456");
    
    // Logout
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
    
    // String
    msg.set(100, "string_value");
    REQUIRE(msg.get_string(100) == "string_value");
    
    // Integer via set(int, int)
    msg.set(101, 42);
    REQUIRE(msg.get_string(101) == "42");
    REQUIRE(msg.get_int(101) == 42);
    
    // Overwrite
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
    
    // 设置空字符串也算存在
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
