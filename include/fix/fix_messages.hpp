#pragma once

#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "base/config.hpp"

namespace fix40 {

/* ------------------------------------------------------------
 * 创建一个 Logon 类型的 FixMessage 对象
 * ---------------------------------------------------------- */
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

/* ------------------------------------------------------------
 * 创建一个 Heartbeat (心跳) 类型的 FixMessage 对象
 * ---------------------------------------------------------- */
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

/* ------------------------------------------------------------
 * 创建一个 Test Request (测试请求) 类型的 FixMessage 对象
 * ---------------------------------------------------------- */
inline FixMessage create_test_request_message(const std::string& sender,
                                              const std::string& target,
                                              int seq_num,
                                              const std::string& test_req_id) {
    FixMessage tr;
    tr.set(tags::MsgType, "1");
    tr.set(tags::SenderCompID, sender);
    tr.set(tags::TargetCompID, target);
    tr.set(tags::MsgSeqNum, seq_num);
    tr.set(tags::TestReqID, test_req_id); // 对于测试请求，TestReqID 是必需的
    return tr;
}

/* ------------------------------------------------------------
 * 创建一个 Logout (会话结束) 类型的 FixMessage 对象
 * ---------------------------------------------------------- */
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
} // fix40 名称空间结束
