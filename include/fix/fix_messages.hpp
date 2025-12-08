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

} // namespace fix40
