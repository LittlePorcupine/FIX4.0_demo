/**
 * @file fix_tags.hpp
 * @brief FIX 4.0 协议标签定义
 *
 * 定义 FIX 协议中使用的标准标签（Tag）编号。
 * 每个标签对应消息中的一个字段。
 */

#pragma once

namespace fix40 {

/**
 * @namespace tags
 * @brief FIX 协议标签常量
 *
 * FIX 消息格式：Tag=Value|Tag=Value|...
 * 其中 | 代表 SOH 分隔符（ASCII 0x01）
 */
namespace tags {

// ============================================================================
// 标准消息头标签 (Standard Header)
// ============================================================================

/// @brief 协议版本标识，固定为 "FIX.4.0"
constexpr int BeginString = 8;

/// @brief 消息体长度（从 Tag 35 到 CheckSum 前的字节数）
constexpr int BodyLength = 9;

/// @brief 消息类型（如 "A"=Logon, "0"=Heartbeat, "5"=Logout）
constexpr int MsgType = 35;

/// @brief 发送方标识符
constexpr int SenderCompID = 49;

/// @brief 接收方标识符
constexpr int TargetCompID = 56;

/// @brief 消息序列号
constexpr int MsgSeqNum = 34;

/// @brief 发送时间（UTC 格式：YYYYMMDD-HH:MM:SS）
constexpr int SendingTime = 52;

// ============================================================================
// 会话层标签 (Session Layer)
// ============================================================================

/// @brief 加密方法（0=无加密）
constexpr int EncryptMethod = 98;

/// @brief 心跳间隔（秒）
constexpr int HeartBtInt = 108;

/// @brief 测试请求标识符
constexpr int TestReqID = 112;

/// @brief 文本消息（用于 Logout 原因等）
constexpr int Text = 58;

// ============================================================================
// 标准消息尾标签 (Standard Trailer)
// ============================================================================

/// @brief 校验和（消息所有字节之和 mod 256，3 位数字）
constexpr int CheckSum = 10;

} // namespace tags
} // namespace fix40
