#include "../catch2/catch.hpp"

#include "fix/session.hpp"
#include "fix/fix_messages.hpp"
#include "fix/fix_tags.hpp"

#include <vector>

using namespace fix40;

namespace {

class RecordingApp : public Application {
public:
    void onLogon(const SessionID&) override {}
    void onLogout(const SessionID&) override {}

    void fromApp(const FixMessage& msg, const SessionID&) override {
        received_seq_.push_back(msg.get_int(tags::MsgSeqNum));
    }

    const std::vector<int>& received_seq() const { return received_seq_; }

private:
    std::vector<int> received_seq_;
};

FixMessage make_business(const std::string& msg_type, int seq_num) {
    FixMessage msg;
    msg.set(tags::MsgType, msg_type);
    msg.set(tags::MsgSeqNum, seq_num);
    return msg;
}

} // namespace

TEST_CASE("Session - Seq gap buffers future messages instead of shutdown", "[session][recovery]") {
    RecordingApp app;

    // 以服务端角色建立到 Established：SERVER 收到 USER001 的 Logon
    auto session = std::make_shared<Session>("SERVER", "PENDING", 30, nullptr, nullptr);
    session->set_application(&app);
    session->start();

    FixMessage logon = create_logon_message("USER001", "SERVER", 1, 30, false);
    session->on_message_received(logon);
    REQUIRE(session->is_running());
    REQUIRE(session->get_recv_seq_num() == 2);

    // 先收到未来消息 Seq=4：应当发送 ResendRequest + 缓存，不应 shutdown
    session->on_message_received(make_business("U5", 4));
    REQUIRE(session->is_running());
    REQUIRE(app.received_seq().empty());
    REQUIRE(session->get_recv_seq_num() == 2);

    // 收到缺失消息 Seq=2、3 后，应按序投递 2、3、4
    session->on_message_received(make_business("U5", 2));
    session->on_message_received(make_business("U5", 3));

    REQUIRE(session->is_running());
    REQUIRE(app.received_seq() == std::vector<int>({2, 3, 4}));
    REQUIRE(session->get_recv_seq_num() == 5);
}

TEST_CASE("Session - LogonAck aligns recv seq in LogonSentState", "[session][recovery]") {
    RecordingApp app;

    // 客户端角色：发送 Logon 后进入 LogonSent，等待服务端确认
    auto session = std::make_shared<Session>("USER001", "SERVER", 30, nullptr, nullptr);
    session->set_application(&app);
    session->start();
    REQUIRE(session->is_running());

    // 模拟服务端按历史会话继续序列号：LogonAck 的 MsgSeqNum=20
    FixMessage logon_ack = create_logon_message("SERVER", "USER001", 20, 30, false);
    session->on_message_received(logon_ack);

    // 应对齐到 MsgSeqNum+1，避免后续业务消息被误判为 gap
    REQUIRE(session->get_recv_seq_num() == 21);
}

