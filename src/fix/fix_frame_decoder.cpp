#include "fix/fix_frame_decoder.hpp"
#include "base/config.hpp"
#include <stdexcept>
#include <iostream>

namespace fix40 {

FixFrameDecoder::FixFrameDecoder(size_t max_buffer_size, size_t max_body_length)
    : max_buffer_size_(max_buffer_size), max_body_length_(max_body_length) {}

bool FixFrameDecoder::can_append(size_t len) const {
    if (buffer_.size() >= max_buffer_size_) return false;
    if (len > max_buffer_size_ - buffer_.size()) return false;
    return true;
}

void FixFrameDecoder::append(const char* data, size_t len) {
    // Safe overflow prevention: use subtraction instead of addition
    // This prevents integer overflow when buffer_.size() + len would exceed SIZE_MAX
    if (buffer_.size() >= max_buffer_size_ || len > max_buffer_size_ - buffer_.size()) {
        // 让上层 Connection 或 Session 来决定如何处理这个错误
        throw std::runtime_error("Buffer size limit exceeded. Closing connection.");
    }
    buffer_.append(data, len);
}

bool FixFrameDecoder::next_message(std::string& message) {
    if (buffer_.empty()) {
        return false;
    }

    // --- FIX 消息分架逻辑 ---
    const auto begin_string_pos = buffer_.find("8=FIX.4.0\x01");
    if (begin_string_pos == std::string::npos) {
        // 未找到有效的开头。为了防止缓冲增长，直接清空
        // 更健壮的实现也许会有不同策略
        buffer_.clear();
        return false;
    }
    if (begin_string_pos > 0) {
        // 丢弃消息开头前的无用数据
        buffer_.erase(0, begin_string_pos);
    }

    const auto body_length_tag_pos = buffer_.find("\x01""9=");
    if (body_length_tag_pos == std::string::npos) return false; // 未得到 BodyLength 标签所需的数据

    const auto body_length_val_pos = body_length_tag_pos + 3;
    const auto body_length_end_pos = buffer_.find('\x01', body_length_val_pos);
    if (body_length_end_pos == std::string::npos) return false; // BodyLength 值数据不足

    int body_length = 0;
    try {
        const std::string body_length_str = buffer_.substr(body_length_val_pos, body_length_end_pos - body_length_val_pos);
        body_length = std::stoi(body_length_str);
        if (body_length < 0 || static_cast<size_t>(body_length) > max_body_length_) { // 基本有效性检查
            throw std::runtime_error("Invalid BodyLength value");
        }
    } catch (const std::exception&) {
        // 如果 BodyLength 无效，这是一个严重的协议错误，我们应该丢弃缓冲区的数据以避免死循环
        buffer_.clear(); 
        // 也许应该向上层报告这个错误
        throw; 
    }
    
    // 计算总的预期消息长度
    const size_t soh_after_body_length_pos = body_length_end_pos + 1;
    const size_t total_msg_len = soh_after_body_length_pos + body_length + 7; // "10=NNN\x01" 为 7 个字符

    if (buffer_.size() < total_msg_len) {
        // 数据不足以形成完整消息
        return false;
    }

    // 已经抽出一个完整消息
    message = buffer_.substr(0, total_msg_len);
    
    // 从缓冲中移除已处理的消息
    buffer_.erase(0, total_msg_len);

    return true;
}

} // namespace fix40 