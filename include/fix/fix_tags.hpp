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

/// @brief 重置序列号标志 (Reset Sequence Number Flag)
/// Y = 双方都将会话序列号重置为 1（通常在一方丢失持久化状态时使用）
constexpr int ResetSeqNumFlag = 141;

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

// ============================================================================
// 账户相关标签 (Account)
// ============================================================================

/// @brief 账户标识 (Account)
constexpr int Account = 1;

// ============================================================================
// 自定义标签 (User Defined Tags, 10000+)
// ============================================================================
// 用于扩展 FIX 协议，支持资金查询等自定义功能

/// @brief 请求ID，用于将响应匹配到请求
constexpr int RequestID = 10001;

/// @brief 账户余额 (静态权益)
constexpr int Balance = 10002;

/// @brief 可用资金
constexpr int Available = 10003;

/// @brief 冻结保证金
constexpr int FrozenMargin = 10004;

/// @brief 占用保证金
constexpr int UsedMargin = 10005;

/// @brief 持仓盈亏 (浮动盈亏)
constexpr int PositionProfit = 10006;

/// @brief 平仓盈亏 (已实现盈亏)
constexpr int CloseProfit = 10007;

/// @brief 动态权益
constexpr int DynamicEquity = 10008;

/// @brief 风险度
constexpr int RiskRatio = 10009;

// ============================================================================
// 持仓查询相关自定义标签
// ============================================================================

/// @brief 合约ID
constexpr int InstrumentID = 10010;

/// @brief 多头持仓
constexpr int LongPosition = 10011;

/// @brief 空头持仓
constexpr int ShortPosition = 10012;

/// @brief 多头均价
constexpr int LongAvgPrice = 10013;

/// @brief 空头均价
constexpr int ShortAvgPrice = 10014;

/// @brief 持仓数量 (用于持仓列表中的单条记录)
constexpr int PositionQty = 10015;

/// @brief 持仓方向 (1=多头, 2=空头)
constexpr int PositionSide = 10016;

/// @brief 持仓均价
constexpr int PositionAvgPrice = 10017;

/// @brief 持仓保证金
constexpr int PositionMargin = 10018;

/// @brief 记录数量 (用于列表响应)
constexpr int NoPositions = 10019;

// ============================================================================
// 推送消息相关自定义标签
// ============================================================================

/// @brief 更新类型 (1=账户更新, 2=持仓更新, 3=成交通知)
constexpr int UpdateType = 10020;

/// @brief 更新原因 (1=行情变化, 2=成交, 3=出入金)
constexpr int UpdateReason = 10021;

// ============================================================================
// 合约搜索相关自定义标签
// ============================================================================

/// @brief 搜索关键字（合约代码前缀）
constexpr int SearchPattern = 10022;

/// @brief 搜索结果数量上限
constexpr int MaxResults = 10023;

/// @brief 搜索结果数量
constexpr int ResultCount = 10024;

/// @brief 合约列表（逗号分隔）
constexpr int InstrumentList = 10025;

/// @brief 交易所代码
constexpr int ExchangeID = 10026;

/// @brief 品种代码
constexpr int ProductID = 10027;

/// @brief 最小变动价位
constexpr int PriceTick = 10028;

/// @brief 合约乘数
constexpr int VolumeMultiple = 10029;

/// @brief 保证金率
constexpr int MarginRate = 10030;

} // namespace tags
} // namespace fix40
