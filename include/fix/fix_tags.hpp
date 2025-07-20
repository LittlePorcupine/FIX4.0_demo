#pragma once

namespace fix40 {
namespace tags {

// 标头标签
constexpr int BeginString = 8;
constexpr int BodyLength = 9;
constexpr int MsgType = 35;
constexpr int SenderCompID = 49;
constexpr int TargetCompID = 56;
constexpr int MsgSeqNum = 34;
constexpr int SendingTime = 52;

// 身份验证体的中间标签
constexpr int EncryptMethod = 98;
constexpr int HeartBtInt = 108;

// 会话层的中间标签
constexpr int TestReqID = 112;

// 通用的中间标签
constexpr int Text = 58;

// 尾部标签
constexpr int CheckSum = 10;

} // tags 名称空间结束
} // fix40 名称空间结束
