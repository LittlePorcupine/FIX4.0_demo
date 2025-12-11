/**
 * @file fix_message_builder.hpp
 * @brief FIX 消息构建辅助函数
 *
 * 提供将内部数据结构转换为 FIX 消息的辅助函数。
 */

#pragma once

#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "app/order.hpp"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace fix40 {

/**
 * @brief 格式化 UTC 时间为 FIX 格式
 * @param tp 时间点
 * @return std::string FIX 格式时间字符串 (YYYYMMDD-HH:MM:SS)
 */
inline std::string formatTransactTime(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    gmtime_r(&time_t_val, &tm_buf);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H:%M:%S");
    return oss.str();
}

/**
 * @brief 将 OrderSide 转换为 FIX 字符串
 */
inline std::string sideToFix(OrderSide side) {
    return side == OrderSide::BUY ? "1" : "2";
}

/**
 * @brief 将 OrderType 转换为 FIX 字符串
 */
inline std::string ordTypeToFix(OrderType type) {
    return type == OrderType::MARKET ? "1" : "2";
}

/**
 * @brief 将 OrderStatus 转换为 FIX 字符串
 */
inline std::string ordStatusToFix(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW: return "0";
        case OrderStatus::PARTIALLY_FILLED: return "1";
        case OrderStatus::FILLED: return "2";
        case OrderStatus::CANCELED: return "4";
        case OrderStatus::PENDING_CANCEL: return "6";
        case OrderStatus::REJECTED: return "8";
        case OrderStatus::PENDING_NEW: return "A";  // FIX 4.2+, 但为了兼容
        default: return "0";
    }
}

/**
 * @brief 将 ExecTransType 转换为 FIX 字符串
 */
inline std::string execTransTypeToFix(ExecTransType type) {
    switch (type) {
        case ExecTransType::NEW: return "0";
        case ExecTransType::CANCEL: return "1";
        case ExecTransType::CORRECT: return "2";
        case ExecTransType::STATUS: return "3";
        default: return "0";
    }
}

/**
 * @brief 将 ExecutionReport 转换为 FIX 消息
 * @param report ExecutionReport 结构
 * @return FixMessage FIX 消息对象
 *
 * 构建 FIX 4.0 ExecutionReport (MsgType=8) 消息。
 */
inline FixMessage buildExecutionReport(const ExecutionReport& report) {
    FixMessage msg;
    
    // MsgType = 8 (ExecutionReport)
    msg.set(tags::MsgType, "8");
    
    // 标识符
    msg.set(tags::OrderID, report.orderID);
    msg.set(tags::ClOrdID, report.clOrdID);
    msg.set(tags::ExecID, report.execID);
    
    if (!report.origClOrdID.empty()) {
        msg.set(tags::OrigClOrdID, report.origClOrdID);
    }
    
    // 执行信息
    msg.set(tags::ExecTransType, execTransTypeToFix(report.execTransType));
    msg.set(tags::OrdStatus, ordStatusToFix(report.ordStatus));
    
    // 订单信息
    msg.set(tags::Symbol, report.symbol);
    msg.set(tags::Side, sideToFix(report.side));
    msg.set(tags::OrderQty, std::to_string(report.orderQty));
    
    if (report.ordType == OrderType::LIMIT && report.price > 0) {
        msg.set(tags::Price, std::to_string(report.price));
    }
    msg.set(tags::OrdType, ordTypeToFix(report.ordType));
    
    // 成交信息
    msg.set(tags::CumQty, std::to_string(report.cumQty));
    msg.set(tags::AvgPx, std::to_string(report.avgPx));
    
    if (report.lastShares > 0) {
        msg.set(tags::LastShares, std::to_string(report.lastShares));
        msg.set(tags::LastPx, std::to_string(report.lastPx));
    }
    
    // 剩余数量（FIX 4.0 没有 LeavesQty，但可以通过 OrderQty - CumQty 计算）
    // 这里我们用 Text 字段传递额外信息
    
    // 时间
    msg.set(tags::TransactTime, formatTransactTime(report.transactTime));
    
    // 拒绝原因
    if (report.ordStatus == OrderStatus::REJECTED && report.ordRejReason != 0) {
        msg.set(tags::OrdRejReason, std::to_string(report.ordRejReason));
    }
    
    // 文本说明
    if (!report.text.empty()) {
        msg.set(tags::Text, report.text);
    }
    
    return msg;
}

} // namespace fix40
