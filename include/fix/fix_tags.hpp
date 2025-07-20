#pragma once

namespace fix40 {
namespace tags {

// Header Tags
constexpr int BeginString = 8;
constexpr int BodyLength = 9;
constexpr int MsgType = 35;
constexpr int SenderCompID = 49;
constexpr int TargetCompID = 56;
constexpr int MsgSeqNum = 34;
constexpr int SendingTime = 52;

// Body Tags (Logon-specific)
constexpr int EncryptMethod = 98;
constexpr int HeartBtInt = 108;

// Body Tags (Session-level)
constexpr int TestReqID = 112;

// General-purpose Body Tags
constexpr int Text = 58;

// Trailer Tag
constexpr int CheckSum = 10;

} // namespace tags
} // namespace fix40