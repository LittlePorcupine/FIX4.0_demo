#include "../catch2/catch.hpp"
#include "fix/fix_frame_decoder.hpp"

using namespace fix40;

TEST_CASE("FixFrameDecoder parse complete message", "[decoder]") {
    FixFrameDecoder decoder(1024, 512);
    
    // 构造一个完整的 FIX 消息
    std::string msg = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01" "10=196\x01";
    decoder.append(msg.c_str(), msg.size());
    
    std::string result;
    REQUIRE(decoder.next_message(result));
    REQUIRE(result == msg);
    
    // 缓冲区应该为空
    REQUIRE_FALSE(decoder.next_message(result));
}

TEST_CASE("FixFrameDecoder handle incomplete message", "[decoder]") {
    FixFrameDecoder decoder(1024, 512);
    
    // 只发送消息的一部分
    std::string partial = "8=FIX.4.0\x01" "9=5\x01";
    decoder.append(partial.c_str(), partial.size());
    
    std::string result;
    REQUIRE_FALSE(decoder.next_message(result));
    
    // 发送剩余部分
    std::string rest = "35=0\x01" "10=196\x01";
    decoder.append(rest.c_str(), rest.size());
    
    REQUIRE(decoder.next_message(result));
}

TEST_CASE("FixFrameDecoder handle multiple messages", "[decoder]") {
    FixFrameDecoder decoder(2048, 512);
    
    std::string msg1 = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01" "10=196\x01";
    std::string msg2 = "8=FIX.4.0\x01" "9=5\x01" "35=1\x01" "10=197\x01";
    
    // 一次性发送两条消息
    std::string combined = msg1 + msg2;
    decoder.append(combined.c_str(), combined.size());
    
    std::string result1, result2;
    REQUIRE(decoder.next_message(result1));
    REQUIRE(decoder.next_message(result2));
    REQUIRE(result1 == msg1);
    REQUIRE(result2 == msg2);
}

TEST_CASE("FixFrameDecoder can_append check", "[decoder]") {
    FixFrameDecoder decoder(100, 50);
    
    REQUIRE(decoder.can_append(50));
    REQUIRE(decoder.can_append(100));
    REQUIRE_FALSE(decoder.can_append(101));
    
    // 填充一些数据
    std::string data(60, 'x');
    decoder.append(data.c_str(), data.size());
    
    REQUIRE(decoder.can_append(40));
    REQUIRE_FALSE(decoder.can_append(41));
}

TEST_CASE("FixFrameDecoder buffer overflow protection", "[decoder]") {
    FixFrameDecoder decoder(100, 50);
    
    // 尝试追加超过限制的数据
    std::string large_data(101, 'x');
    
    REQUIRE_FALSE(decoder.can_append(large_data.size()));
    REQUIRE_THROWS(decoder.append(large_data.c_str(), large_data.size()));
}

TEST_CASE("FixFrameDecoder discard garbage before message", "[decoder]") {
    FixFrameDecoder decoder(1024, 512);
    
    // 消息前有垃圾数据
    std::string garbage = "garbage";
    std::string msg = "8=FIX.4.0\x01" "9=5\x01" "35=0\x01" "10=196\x01";
    
    decoder.append(garbage.c_str(), garbage.size());
    decoder.append(msg.c_str(), msg.size());
    
    std::string result;
    REQUIRE(decoder.next_message(result));
    REQUIRE(result == msg);
}
