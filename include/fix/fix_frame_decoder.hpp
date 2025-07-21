#pragma once

#include <string>
#include <vector>

namespace fix40 {

class FixFrameDecoder {
public:
    // 向内部缓冲区追加新的数据
    void append(const char* data, size_t len);

    // 尝试从缓冲区中解析出下一条完整的 FIX 消息
    // 如果成功，消息内容会被放入 message 参数，并返回 true
    // 如果数据不足以构成一条完整消息，则返回 false
    bool next_message(std::string& message);

private:
    std::string buffer_;
};

} // namespace fix40 