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

// ============================================================================
// 业务层标签 (Business Layer) - 标识符
// ============================================================================

/// @brief 客户端订单ID (Client Order ID)，必填，客户端生成用于唯一标识订单
constexpr int ClOrdID = 11;

/// @brief 交易所/服务端订单ID (Order ID)，服务端生成
constexpr int OrderID = 37;

/// @brief 执行ID (Execution ID)，每一笔成交或状态变化都应有唯一的执行ID
constexpr int ExecID = 17;

/// @brief 原始客户端订单ID (Original Client Order ID)，撤单/改单时引用原订单
constexpr int OrigClOrdID = 41;

/// @brief 执行引用ID (Exec Ref ID)，当 ExecTransType 为 Cancel/Correct 时引用前一次的 ExecID
constexpr int ExecRefID = 19;

// ============================================================================
// 业务层标签 (Business Layer) - 订单细节
// ============================================================================

/// @brief 处理指令 (Handling Instructions)，FIX 4.0 必填。通常 '1'=Automated execution
constexpr int HandlInst = 21;

/// @brief 标的代码 (Symbol)，如 "IF2305", "AAPL"
constexpr int Symbol = 55;

/// @brief 买卖方向 (Side): 1=Buy, 2=Sell
constexpr int Side = 54;

/// @brief 订单数量 (Order Quantity)
constexpr int OrderQty = 38;

/// @brief 价格 (Price)，限价单必填
constexpr int Price = 44;

/// @brief 订单类型 (Order Type): 1=Market, 2=Limit
constexpr int OrdType = 40;

/// @brief 交易时间 (Transaction Time)，UTC 时间
constexpr int TransactTime = 60;

/// @brief 订单有效时间 (Time In Force): 0=Day, 1=GTC, 3=IOC, 4=FOK
constexpr int TimeInForce = 59;

/// @brief 撤单类型 (Cancel Type)，FIX 4.0 撤单消息(F)必填。'F'=Full remaining quantity
constexpr int CxlType = 125;

// ============================================================================
// 业务层标签 (Business Layer) - 执行结果
// ============================================================================

/// @brief 订单状态 (Order Status)
/// 0=New, 1=Partially Filled, 2=Filled, 4=Canceled, 8=Rejected
constexpr int OrdStatus = 39;

/// @brief 执行事务类型 (Execution Transaction Type) - FIX 4.0 核心字段
/// 0=New, 1=Cancel, 2=Correct, 3=Status
/// 注意：FIX 4.0 没有 ExecType(150)，状态变化由本字段和 OrdStatus 共同表达
constexpr int ExecTransType = 20;

/// @brief 累计成交数量 (Cumulative Quantity)
constexpr int CumQty = 14;

/// @brief 平均成交价格 (Average Price)
constexpr int AvgPx = 6;

/// @brief 本次成交数量 (Last Shares) - FIX 4.0 用于表达单次成交量
constexpr int LastShares = 32;

/// @brief 本次成交价格 (Last Price) - FIX 4.0 用于表达单次成交价
constexpr int LastPx = 31;

/// @brief 订单拒绝原因 (Order Reject Reason)
constexpr int OrdRejReason = 103;

// ============================================================================
// 会话层标签 - 断线恢复相关
// ============================================================================

/// @brief 起始序列号 (Begin Sequence Number)，用于 ResendRequest
constexpr int BeginSeqNo = 7;

/// @brief 结束序列号 (End Sequence Number)，用于 ResendRequest
constexpr int EndSeqNo = 16;

/// @brief 新序列号 (New Sequence Number)，用于 SequenceReset
constexpr int NewSeqNo = 36;

/// @brief GapFill 标志 (Gap Fill Flag)，用于 SequenceReset
/// Y = Gap Fill 模式，N = Reset 模式
constexpr int GapFillFlag = 123;

/// @brief 可能重复标志 (Possible Duplicate Flag)
/// Y = 消息可能是重复的
constexpr int PossDupFlag = 43;

/// @brief 原始发送时间 (Original Sending Time)
/// 重传消息时使用，记录原始发送时间
constexpr int OrigSendingTime = 122;

} // namespace tags
} // namespace fix40
