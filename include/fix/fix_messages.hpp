/**
 * @file fix_messages.hpp
 * @brief FIX 会话层消息工厂函数
 *
 * 提供创建标准 FIX 会话层消息的便捷函数，包括：
 * - Logon (A) - 登录
 * - Heartbeat (0) - 心跳
 * - TestRequest (1) - 测试请求
 * - Logout (5) - 登出
 */

#pragma once

#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "base/config.hpp"

namespace fix40 {

/**
 * @brief 创建 Logon 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号（默认为 1）
 * @param heart_bt 心跳间隔秒数（默认从配置读取）
 * @return FixMessage Logon 消息对象
 *
 * Logon 消息用于建立 FIX 会话，包含：
 * - MsgType (35) = "A"
 * - EncryptMethod (98) = "0" (无加密)
 * - HeartBtInt (108) = 心跳间隔
 */
inline FixMessage create_logon_message(const std::string& sender,
                                       const std::string& target,
                                       int seq_num = 1,
                                       int heart_bt = Config::instance().get_int("fix_session", "default_heartbeat_interval", 30)) {
    FixMessage logon;
    logon.set(tags::MsgType, "A");
    logon.set(tags::EncryptMethod, "0");
    logon.set(tags::HeartBtInt, heart_bt);
    logon.set(tags::SenderCompID, sender);
    logon.set(tags::TargetCompID, target);
    logon.set(tags::MsgSeqNum, seq_num);
    return logon;
}

/**
 * @brief 创建 Heartbeat 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号
 * @param test_req_id TestReqID（响应 TestRequest 时填写，否则为空）
 * @return FixMessage Heartbeat 消息对象
 *
 * Heartbeat 消息用于：
 * 1. 定期发送以维持连接活跃
 * 2. 响应 TestRequest（此时需包含对应的 TestReqID）
 *
 * - MsgType (35) = "0"
 * - TestReqID (112) = 可选
 */
inline FixMessage create_heartbeat_message(const std::string& sender,
                                           const std::string& target,
                                           int seq_num,
                                           const std::string& test_req_id = "") {
    FixMessage hb;
    hb.set(tags::MsgType, "0");
    hb.set(tags::SenderCompID, sender);
    hb.set(tags::TargetCompID, target);
    hb.set(tags::MsgSeqNum, seq_num);

    if (!test_req_id.empty()) {
        hb.set(tags::TestReqID, test_req_id);
    }

    return hb;
}

/**
 * @brief 创建 TestRequest 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号
 * @param test_req_id 测试请求标识符（必填，对方需在 Heartbeat 中回传）
 * @return FixMessage TestRequest 消息对象
 *
 * TestRequest 消息用于检测对端是否存活。
 * 对端收到后应回复包含相同 TestReqID 的 Heartbeat。
 *
 * - MsgType (35) = "1"
 * - TestReqID (112) = 必填
 */
inline FixMessage create_test_request_message(const std::string& sender,
                                              const std::string& target,
                                              int seq_num,
                                              const std::string& test_req_id) {
    FixMessage tr;
    tr.set(tags::MsgType, "1");
    tr.set(tags::SenderCompID, sender);
    tr.set(tags::TargetCompID, target);
    tr.set(tags::MsgSeqNum, seq_num);
    tr.set(tags::TestReqID, test_req_id);
    return tr;
}

/**
 * @brief 创建 Logout 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号
 * @param text 登出原因（可选）
 * @return FixMessage Logout 消息对象
 *
 * Logout 消息用于优雅地终止 FIX 会话。
 * 发起方发送 Logout 后等待对方确认，然后关闭连接。
 *
 * - MsgType (35) = "5"
 * - Text (58) = 可选，说明登出原因
 */
inline FixMessage create_logout_message(const std::string& sender,
                                        const std::string& target,
                                        int seq_num,
                                        const std::string& text = "") {
    FixMessage lo;
    lo.set(tags::MsgType, "5");
    lo.set(tags::SenderCompID, sender);
    lo.set(tags::TargetCompID, target);
    lo.set(tags::MsgSeqNum, seq_num);
    if (!text.empty()) {
        lo.set(tags::Text, text);
    }
    return lo;
}

/**
 * @brief 创建 ResendRequest 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号
 * @param begin_seq_no 请求重传的起始序列号
 * @param end_seq_no 请求重传的结束序列号（0 表示到最新）
 * @return FixMessage ResendRequest 消息对象
 *
 * ResendRequest 消息用于请求对方重传指定范围的消息。
 * 当检测到序列号 gap 时发送此消息。
 *
 * - MsgType (35) = "2"
 * - BeginSeqNo (7) = 起始序列号
 * - EndSeqNo (16) = 结束序列号（0 表示无限）
 */
inline FixMessage create_resend_request_message(const std::string& sender,
                                                 const std::string& target,
                                                 int seq_num,
                                                 int begin_seq_no,
                                                 int end_seq_no) {
    FixMessage rr;
    rr.set(tags::MsgType, "2");
    rr.set(tags::SenderCompID, sender);
    rr.set(tags::TargetCompID, target);
    rr.set(tags::MsgSeqNum, seq_num);
    rr.set(tags::BeginSeqNo, begin_seq_no);
    rr.set(tags::EndSeqNo, end_seq_no);
    return rr;
}

/**
 * @brief 创建 SequenceReset 消息
 * @param sender 发送方 CompID
 * @param target 接收方 CompID
 * @param seq_num 消息序列号
 * @param new_seq_no 新的序列号
 * @param gap_fill 是否为 GapFill 模式
 * @return FixMessage SequenceReset 消息对象
 *
 * SequenceReset 消息用于：
 * 1. GapFill 模式：跳过管理消息（如 Heartbeat、TestRequest）
 * 2. Reset 模式：重置序列号（通常在会话重置时使用）
 *
 * - MsgType (35) = "4"
 * - NewSeqNo (36) = 新序列号
 * - GapFillFlag (123) = Y/N
 */
inline FixMessage create_sequence_reset_message(const std::string& sender,
                                                 const std::string& target,
                                                 int seq_num,
                                                 int new_seq_no,
                                                 bool gap_fill = true) {
    FixMessage sr;
    sr.set(tags::MsgType, "4");
    sr.set(tags::SenderCompID, sender);
    sr.set(tags::TargetCompID, target);
    sr.set(tags::MsgSeqNum, seq_num);
    sr.set(tags::NewSeqNo, new_seq_no);
    sr.set(tags::GapFillFlag, gap_fill ? "Y" : "N");
    return sr;
}

/**
 * @brief 判断消息类型是否为管理消息
 * @param msg_type 消息类型
 * @return true 如果是管理消息（Heartbeat、TestRequest、ResendRequest、SequenceReset、Logout、Logon）
 *
 * 管理消息在重传时应使用 SequenceReset-GapFill 跳过，而不是重新发送。
 */
inline bool is_admin_message(const std::string& msg_type) {
    return msg_type == "0" ||  // Heartbeat
           msg_type == "1" ||  // TestRequest
           msg_type == "2" ||  // ResendRequest
           msg_type == "4" ||  // SequenceReset
           msg_type == "5" ||  // Logout
           msg_type == "A";    // Logon
}

} // namespace fix40
