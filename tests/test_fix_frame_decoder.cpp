#include "fix/fix_frame_decoder.hpp"
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <climits>
#include <string>
#include <functional>
#include <cstring>

using namespace fix40;

// Simple test framework
class TestRunner {
public:
    static void run_test(const std::string& test_name, void (*test_func)()) {
        std::cout << "Running " << test_name << "... ";
        try {
            test_func();
            std::cout << "PASSED" << std::endl;
            passed_++;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
            failed_++;
        } catch (...) {
            std::cout << "FAILED: Unknown exception" << std::endl;
            failed_++;
        }
        total_++;
    }
    
    static void print_summary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total: " << total_ << ", Passed: " << passed_ << ", Failed: " << failed_ << std::endl;
        if (failed_ > 0) {
            std::cout << "Some tests FAILED!" << std::endl;
        } else {
            std::cout << "All tests PASSED!" << std::endl;
        }
    }
    
    static int get_failed_count() { return failed_; }

private:
    static int total_;
    static int passed_;
    static int failed_;
};

int TestRunner::total_ = 0;
int TestRunner::passed_ = 0;
int TestRunner::failed_ = 0;

// Test helper functions
void assert_throws(const std::string& message, std::function<void()> func) {
    try {
        func();
        throw std::runtime_error("Expected exception but none was thrown: " + message);
    } catch (const std::runtime_error& e) {
        // Expected exception - check if it's the right type
        std::string error_msg = e.what();
        if (error_msg.find("Buffer size limit exceeded") == std::string::npos) {
            throw std::runtime_error("Wrong exception type: " + error_msg);
        }
    }
}

void assert_no_throw(const std::string& message, std::function<void()> func) {
    try {
        func();
    } catch (const std::exception& e) {
        throw std::runtime_error("Unexpected exception: " + message + " - " + e.what());
    }
}

// Test cases for buffer overflow prevention scenarios

void test_prevent_integer_overflow_with_large_length() {
    // Test with SIZE_MAX - 1 length to trigger overflow prevention
    FixFrameDecoder decoder(1000, 500);
    
    const char* data = "test";
    size_t large_len = SIZE_MAX - 1;
    
    assert_throws("Should prevent integer overflow with SIZE_MAX-1 length", [&]() {
        decoder.append(data, large_len);
    });
}

void test_prevent_overflow_with_buffer_near_max_size() {
    // Test with buffer near max size and additional data that would exceed limit
    const size_t max_buffer = 100;
    FixFrameDecoder decoder(max_buffer, 50);
    
    // Fill buffer to near capacity
    std::string large_data(90, 'x');
    decoder.append(large_data.c_str(), large_data.size());
    
    // Try to add more data that would exceed the limit
    const char* additional_data = "this will exceed";
    assert_throws("Should prevent buffer overflow when near max size", [&]() {
        decoder.append(additional_data, strlen(additional_data));
    });
}

void test_prevent_overflow_exact_boundary() {
    // Test exact boundary conditions
    const size_t max_buffer = 50;
    FixFrameDecoder decoder(max_buffer, 25);
    
    // Fill to exact capacity - should work
    std::string exact_data(max_buffer, 'a');
    assert_no_throw("Should allow data up to exact max buffer size", [&]() {
        decoder.append(exact_data.c_str(), exact_data.size());
    });
    
    // Try to add one more byte - should fail
    const char one_byte = 'x';
    assert_throws("Should prevent adding even one byte over limit", [&]() {
        decoder.append(&one_byte, 1);
    });
}

void test_prevent_overflow_with_zero_length_at_max() {
    // Test edge case: buffer at max size, trying to append zero length
    const size_t max_buffer = 10;
    FixFrameDecoder decoder(max_buffer, 5);
    
    std::string max_data(max_buffer, 'z');
    decoder.append(max_data.c_str(), max_data.size());
    
    // Appending zero length should still fail because buffer is at max
    assert_throws("Should prevent append of zero length when buffer is at max", [&]() {
        decoder.append("", 0);
    });
}

void test_safe_subtraction_logic() {
    // Test that the safe subtraction logic works correctly
    const size_t max_buffer = 1000;
    FixFrameDecoder decoder(max_buffer, 500);
    
    // Test various sizes that should work
    std::string data1(100, 'a');
    std::string data2(200, 'b');
    std::string data3(300, 'c');
    
    assert_no_throw("First append should work", [&]() {
        decoder.append(data1.c_str(), data1.size());
    });
    
    assert_no_throw("Second append should work", [&]() {
        decoder.append(data2.c_str(), data2.size());
    });
    
    assert_no_throw("Third append should work", [&]() {
        decoder.append(data3.c_str(), data3.size());
    });
    
    // Now buffer has 600 bytes, max is 1000, so 400 more should work
    std::string data4(400, 'd');
    assert_no_throw("Fourth append at exact limit should work", [&]() {
        decoder.append(data4.c_str(), data4.size());
    });
    
    // One more byte should fail
    assert_throws("One more byte should fail", [&]() {
        decoder.append("x", 1);
    });
}

// Test cases for edge cases with maximum buffer sizes and large input lengths

void test_maximum_safe_buffer_size() {
    // Test with a very large but safe buffer size
    const size_t large_buffer = SIZE_MAX / 4;  // Use a quarter of max to be safe
    FixFrameDecoder decoder(large_buffer, large_buffer / 2);
    
    // Should be able to append reasonable amounts of data
    std::string reasonable_data(1000, 'x');
    assert_no_throw("Should handle reasonable data with large buffer", [&]() {
        decoder.append(reasonable_data.c_str(), reasonable_data.size());
    });
}

void test_large_single_append() {
    // Test appending a single large chunk that exceeds buffer limit
    const size_t max_buffer = 1000;
    FixFrameDecoder decoder(max_buffer, 500);
    
    std::string oversized_data(max_buffer + 1, 'x');
    assert_throws("Should reject single append larger than buffer", [&]() {
        decoder.append(oversized_data.c_str(), oversized_data.size());
    });
}

void test_multiple_small_appends_exceeding_limit() {
    // Test multiple small appends that cumulatively exceed the limit
    const size_t max_buffer = 100;
    FixFrameDecoder decoder(max_buffer, 50);
    
    // Add data in small chunks
    for (int i = 0; i < 10; i++) {
        std::string chunk(9, 'a' + i);  // 9 bytes each, total 90 bytes
        assert_no_throw("Small chunks should work initially", [&]() {
            decoder.append(chunk.c_str(), chunk.size());
        });
    }
    
    // Now buffer has 90 bytes, adding 11 more should fail
    std::string final_chunk(11, 'z');
    assert_throws("Final chunk exceeding limit should fail", [&]() {
        decoder.append(final_chunk.c_str(), final_chunk.size());
    });
}

// Test cases to verify normal operation continues to work correctly after fix

void test_normal_operation_small_messages() {
    // Test that normal FIX message processing still works
    const size_t max_buffer = 2000;
    FixFrameDecoder decoder(max_buffer, 1000);
    
    // Create a valid FIX message with correct BodyLength calculation
    // Body part: "35=A\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01" "52=20240101-12:00:00\x01"
    std::string body = "35=A\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01" "52=20240101-12:00:00\x01";
    std::string body_length = std::to_string(body.length());
    std::string fix_msg = "8=FIX.4.0\x01" "9=" + body_length + "\x01" + body + "10=123\x01";
    
    assert_no_throw("Normal FIX message should be accepted", [&]() {
        decoder.append(fix_msg.c_str(), fix_msg.size());
    });
    
    // Should be able to extract the message
    std::string extracted_msg;
    bool result = decoder.next_message(extracted_msg);
    assert(result && "Should be able to extract normal FIX message");
    assert(extracted_msg == fix_msg && "Extracted message should match input");
}

void test_normal_operation_incremental_building() {
    // Test building a message incrementally (normal use case)
    const size_t max_buffer = 1000;
    FixFrameDecoder decoder(max_buffer, 500);
    
    // Calculate correct body length first
    std::string body = "35=0\x01" "49=TEST\x01" "56=PEER\x01" "34=1\x01";
    std::string body_length = std::to_string(body.length());
    
    // Add message parts incrementally
    assert_no_throw("Should accept message header", [&]() {
        decoder.append("8=FIX.4.0\x01", 10);
    });
    
    assert_no_throw("Should accept body length", [&]() {
        std::string body_len_part = "9=" + body_length + "\x01";
        decoder.append(body_len_part.c_str(), body_len_part.size());
    });
    
    assert_no_throw("Should accept message body", [&]() {
        decoder.append(body.c_str(), body.size());
    });
    
    assert_no_throw("Should accept checksum", [&]() {
        decoder.append("10=123\x01", 7);
    });
    
    // Should be able to extract complete message
    std::string extracted_msg;
    bool result = decoder.next_message(extracted_msg);
    assert(result && "Should extract incrementally built message");
}

void test_normal_operation_multiple_messages() {
    // Test processing multiple messages in sequence
    const size_t max_buffer = 3000;
    FixFrameDecoder decoder(max_buffer, 1000);
    
    // Create messages with correct body lengths
    std::string body1 = "35=0\x01" "49=TEST1\x01" "56=PEER\x01" "34=1\x01";
    std::string body2 = "35=0\x01" "49=TEST2\x01" "56=PEER\x01" "34=2\x01";
    
    std::string msg1 = "8=FIX.4.0\x01" "9=" + std::to_string(body1.length()) + "\x01" + body1 + "10=123\x01";
    std::string msg2 = "8=FIX.4.0\x01" "9=" + std::to_string(body2.length()) + "\x01" + body2 + "10=124\x01";
    
    assert_no_throw("Should accept first message", [&]() {
        decoder.append(msg1.c_str(), msg1.size());
    });
    
    assert_no_throw("Should accept second message", [&]() {
        decoder.append(msg2.c_str(), msg2.size());
    });
    
    // Extract both messages
    std::string extracted1, extracted2;
    assert(decoder.next_message(extracted1) && "Should extract first message");
    assert(decoder.next_message(extracted2) && "Should extract second message");
    assert(extracted1 == msg1 && "First message should match");
    assert(extracted2 == msg2 && "Second message should match");
}

void test_normal_operation_after_failed_append() {
    // Test that decoder continues to work normally after a failed append
    const size_t max_buffer = 100;
    FixFrameDecoder decoder(max_buffer, 50);
    
    // Try to add oversized data - should fail
    std::string oversized(max_buffer + 1, 'x');
    assert_throws("Oversized append should fail", [&]() {
        decoder.append(oversized.c_str(), oversized.size());
    });
    
    // Normal operation should still work
    std::string body = "35=0\x01" "49=OK\x01" "56=PEER\x01";
    std::string normal_msg = "8=FIX.4.0\x01" "9=" + std::to_string(body.length()) + "\x01" + body + "10=123\x01";
    assert_no_throw("Normal message should work after failed append", [&]() {
        decoder.append(normal_msg.c_str(), normal_msg.size());
    });
    
    std::string extracted;
    assert(decoder.next_message(extracted) && "Should extract message after failed append");
}

void test_buffer_management_after_message_extraction() {
    // Test that buffer is properly managed after message extraction
    const size_t max_buffer = 200;
    FixFrameDecoder decoder(max_buffer, 100);
    
    // Add a message and extract it
    std::string body = "35=0\x01" "49=TEST\x01" "56=PEER\x01" "34=1\x01";
    std::string msg = "8=FIX.4.0\x01" "9=" + std::to_string(body.length()) + "\x01" + body + "10=123\x01";
    decoder.append(msg.c_str(), msg.size());
    
    std::string extracted;
    decoder.next_message(extracted);
    
    // Should be able to add more data after extraction (buffer should be cleared)
    std::string large_data(max_buffer - 10, 'y');  // Almost fill buffer
    assert_no_throw("Should accept large data after message extraction", [&]() {
        decoder.append(large_data.c_str(), large_data.size());
    });
}

int main() {
    std::cout << "=== FixFrameDecoder Security Fix Tests ===" << std::endl;
    std::cout << "Testing buffer overflow prevention scenarios..." << std::endl;
    
    // Buffer overflow prevention tests
    TestRunner::run_test("test_prevent_integer_overflow_with_large_length", test_prevent_integer_overflow_with_large_length);
    TestRunner::run_test("test_prevent_overflow_with_buffer_near_max_size", test_prevent_overflow_with_buffer_near_max_size);
    TestRunner::run_test("test_prevent_overflow_exact_boundary", test_prevent_overflow_exact_boundary);
    TestRunner::run_test("test_prevent_overflow_with_zero_length_at_max", test_prevent_overflow_with_zero_length_at_max);
    TestRunner::run_test("test_safe_subtraction_logic", test_safe_subtraction_logic);
    
    // Edge cases with maximum buffer sizes and large input lengths
    TestRunner::run_test("test_maximum_safe_buffer_size", test_maximum_safe_buffer_size);
    TestRunner::run_test("test_large_single_append", test_large_single_append);
    TestRunner::run_test("test_multiple_small_appends_exceeding_limit", test_multiple_small_appends_exceeding_limit);
    
    // Normal operation verification tests
    TestRunner::run_test("test_normal_operation_small_messages", test_normal_operation_small_messages);
    TestRunner::run_test("test_normal_operation_incremental_building", test_normal_operation_incremental_building);
    TestRunner::run_test("test_normal_operation_multiple_messages", test_normal_operation_multiple_messages);
    TestRunner::run_test("test_normal_operation_after_failed_append", test_normal_operation_after_failed_append);
    TestRunner::run_test("test_buffer_management_after_message_extraction", test_buffer_management_after_message_extraction);
    
    TestRunner::print_summary();
    return TestRunner::get_failed_count();
}