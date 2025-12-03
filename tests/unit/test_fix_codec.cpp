#include "../catch2/catch.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_messages.hpp"

using namespace fix40;

TEST_CASE("FixCodec encode basic message", "[codec]") {
    FixCodec codec;
    auto msg = create_logon_message("CLIENT", "SERVER", 1, 30);
    
    std::string encoded = codec.encode(msg);
    
    // 检查必要字段存在
    REQUIRE(encoded.find("8=FIX.4.0") != std::string::npos);
    REQUIRE(encoded.find("35=A") != std::string::npos);
    REQUIRE(encoded.find("49=CLIENT") != std::string::npos);
    REQUIRE(encoded.find("56=SERVER") != std::string::npos);
    REQUIRE(encoded.find("108=30") != std::string::npos);
    
    // 检查以 SOH 结尾
    REQUIRE(encoded.back() == '\x01');
}

TEST_CASE("FixCodec decode valid message", "[codec]") {
    FixCodec codec;
    
    // 先编码再解码
    auto original = create_heartbeat_message("SENDER", "TARGET", 1, "");
    std::string encoded = codec.encode(original);
    
    FixMessage decoded = codec.decode(encoded);
    
    REQUIRE(decoded.get_string(tags::MsgType) == "0");
    REQUIRE(decoded.get_string(tags::SenderCompID) == "SENDER");
    REQUIRE(decoded.get_string(tags::TargetCompID) == "TARGET");
}

TEST_CASE("FixCodec checksum validation", "[codec]") {
    FixCodec codec;
    
    // 构造一个校验和错误的消息
    std::string bad_msg = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01" "10=000\x01";
    
    REQUIRE_THROWS_AS(codec.decode(bad_msg), std::runtime_error);
}

TEST_CASE("FixCodec body length validation", "[codec]") {
    FixCodec codec;
    auto msg = create_logon_message("A", "B", 1, 30);
    std::string encoded = codec.encode(msg);
    
    // 解码应该成功，body length 正确
    REQUIRE_NOTHROW(codec.decode(encoded));
}

TEST_CASE("FixMessage field access", "[codec]") {
    FixMessage msg;
    
    msg.set(tags::MsgType, "A");
    msg.set(tags::MsgSeqNum, 42);
    
    REQUIRE(msg.get_string(tags::MsgType) == "A");
    REQUIRE(msg.get_int(tags::MsgSeqNum) == 42);
    REQUIRE(msg.has(tags::MsgType));
    REQUIRE_FALSE(msg.has(tags::Text));
    
    // 访问不存在的字段应该抛异常
    REQUIRE_THROWS_AS(msg.get_string(tags::Text), std::runtime_error);
}
