/**
 * @file fix_frame_decoder.hpp
 * @brief FIX 消息帧解码器
 *
 * 处理 TCP 流的粘包/拆包问题，从字节流中提取完整的 FIX 消息。
 */

#pragma once

#include <string>
#include <vector>

namespace fix40 {

/**
 * @class FixFrameDecoder
 * @brief FIX 消息帧解码器
 *
 * 由于 TCP 是流式协议，接收到的数据可能包含多条消息（粘包）
 * 或不完整的消息（拆包）。该类负责缓存数据并提取完整的 FIX 消息。
 *
 * @par 工作原理
 * 1. 查找消息起始标记 "8=FIX.4.0\x01"
 * 2. 解析 BodyLength (9=) 获取消息体长度
 * 3. 计算完整消息长度并提取
 *
 * @par 使用示例
 * @code
 * FixFrameDecoder decoder(1048576, 4096);
 * 
 * // 接收数据
 * decoder.append(buffer, bytes_read);
 * 
 * // 提取完整消息
 * std::string msg;
 * while (decoder.next_message(msg)) {
 *     process_message(msg);
 * }
 * @endcode
 */
class FixFrameDecoder {
public:
    /**
     * @brief 构造帧解码器
     * @param max_buffer_size 缓冲区最大大小（字节），防止内存耗尽
     * @param max_body_length 消息体最大长度（字节），防止解析超长消息
     */
    explicit FixFrameDecoder(size_t max_buffer_size, size_t max_body_length);

    /**
     * @brief 检查是否可以追加指定长度的数据
     * @param len 要追加的数据长度
     * @return true 可以追加（不会溢出）
     * @return false 追加后会超出缓冲区限制
     */
    bool can_append(size_t len) const;

    /**
     * @brief 向内部缓冲区追加数据
     * @param data 数据指针
     * @param len 数据长度
     * @throws std::runtime_error 缓冲区溢出时抛出异常
     */
    void append(const char* data, size_t len);

    /**
     * @brief 尝试从缓冲区提取下一条完整消息
     * @param[out] message 输出参数，存放提取的消息
     * @return true 成功提取一条完整消息
     * @return false 数据不足，无法提取完整消息
     *
     * @note 应循环调用直到返回 false，以处理粘包情况
     * @throws std::runtime_error BodyLength 无效时抛出异常
     */
    bool next_message(std::string& message);

private:
    std::string buffer_;              ///< 内部数据缓冲区
    const size_t max_buffer_size_;    ///< 缓冲区最大大小
    const size_t max_body_length_;    ///< 消息体最大长度
};

} // namespace fix40 