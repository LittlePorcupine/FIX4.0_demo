#include "../catch2/catch.hpp"
#include "fix/application.hpp"
#include "fix/session.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "fix/fix_messages.hpp"
#include "app/simulation_app.hpp"
#include "app/matching_engine.hpp"

using namespace fix40;

// 测试用的 Mock Application
class MockApplication : public Application {
public:
    int logon_count = 0;
    int logout_count = 0;
    int from_app_count = 0;
    int to_app_count = 0;
    int from_admin_count = 0;
    int to_admin_count = 0;
    std::string last_msg_type;
    SessionID last_session_id;
    
    void onLogon(const SessionID& sessionID) override {
        logon_count++;
        last_session_id = sessionID;
    }
    
    void onLogout(const SessionID& sessionID) override {
        logout_count++;
        last_session_id = sessionID;
    }
    
    void fromApp(const FixMessage& msg, const SessionID& sessionID) override {
        from_app_count++;
        last_msg_type = msg.get_string(tags::MsgType);
        last_session_id = sessionID;
    }
    
    void toApp(FixMessage&, const SessionID& sessionID) override {
        to_app_count++;
        last_session_id = sessionID;
    }
    
    void fromAdmin(const FixMessage&, const SessionID& sessionID) override {
        from_admin_count++;
        last_session_id = sessionID;
    }
    
    void toAdmin(FixMessage&, const SessionID& sessionID) override {
        to_admin_count++;
        last_session_id = sessionID;
    }
};

// 会抛出异常的 Application，用于测试异常安全
class ThrowingApplication : public Application {
public:
    void onLogon(const SessionID&) override {
        throw std::runtime_error("onLogon exception");
    }
    
    void onLogout(const SessionID&) override {
        throw std::runtime_error("onLogout exception");
    }
    
    void fromApp(const FixMessage&, const SessionID&) override {
        throw std::runtime_error("fromApp exception");
    }
    
    void toApp(FixMessage&, const SessionID&) override {
        throw std::runtime_error("toApp exception");
    }
};

TEST_CASE("SessionID basic operations", "[application][sessionid]") {
    SECTION("Default constructor") {
        SessionID id;
        REQUIRE(id.senderCompID.empty());
        REQUIRE(id.targetCompID.empty());
    }
    
    SECTION("Parameterized constructor") {
        SessionID id("SENDER", "TARGET");
        REQUIRE(id.senderCompID == "SENDER");
        REQUIRE(id.targetCompID == "TARGET");
    }
    
    SECTION("to_string") {
        SessionID id("CLIENT", "SERVER");
        REQUIRE(id.to_string() == "CLIENT->SERVER");
    }
    
    SECTION("Equality comparison") {
        SessionID id1("A", "B");
        SessionID id2("A", "B");
        SessionID id3("A", "C");
        
        REQUIRE(id1 == id2);
        REQUIRE(id1 != id3);
    }
}

TEST_CASE("Application interface - set and get", "[application]") {
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    
    SECTION("Application is null by default") {
        REQUIRE(session->get_application() == nullptr);
    }
    
    SECTION("Can set and get application") {
        MockApplication app;
        session->set_application(&app);
        REQUIRE(session->get_application() == &app);
    }
    
    SECTION("Can set application to null") {
        MockApplication app;
        session->set_application(&app);
        session->set_application(nullptr);
        REQUIRE(session->get_application() == nullptr);
    }
}

TEST_CASE("Session get_session_id", "[application][session]") {
    auto session = std::make_shared<Session>("MY_CLIENT", "MY_SERVER", 30, [](){});
    
    SessionID id = session->get_session_id();
    REQUIRE(id.senderCompID == "MY_CLIENT");
    REQUIRE(id.targetCompID == "MY_SERVER");
}

TEST_CASE("Application callbacks - onLogon with SessionID", "[application]") {
    MockApplication app;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 模拟收到 Logon 确认
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    
    session->on_message_received(logon_ack);
    
    REQUIRE(app.logon_count == 1);
    REQUIRE(app.last_session_id.senderCompID == "CLIENT");
    REQUIRE(app.last_session_id.targetCompID == "SERVER");
}

TEST_CASE("Application callbacks - onLogout with SessionID", "[application]") {
    MockApplication app;
    bool shutdown_called = false;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [&](){ shutdown_called = true; });
    session->set_application(&app);
    session->start();
    
    // 先建立会话
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    session->on_message_received(logon_ack);
    
    // 触发关闭
    session->on_shutdown("Test shutdown");
    
    REQUIRE(app.logout_count == 1);
    REQUIRE(app.last_session_id.senderCompID == "CLIENT");
    REQUIRE(shutdown_called);
}

TEST_CASE("Application callbacks - fromApp with SessionID", "[application]") {
    MockApplication app;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 先建立会话
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    session->on_message_received(logon_ack);
    
    // 发送业务消息
    FixMessage order;
    order.set(tags::MsgType, "D");
    order.set(tags::SenderCompID, "SERVER");
    order.set(tags::TargetCompID, "CLIENT");
    order.set(tags::MsgSeqNum, 2);
    
    session->on_message_received(order);
    
    REQUIRE(app.from_app_count == 1);
    REQUIRE(app.last_msg_type == "D");
    REQUIRE(app.last_session_id.senderCompID == "CLIENT");
}

TEST_CASE("Application callbacks - fromAdmin for session messages", "[application]") {
    MockApplication app;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 先建立会话
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    session->on_message_received(logon_ack);
    
    // 收到 Heartbeat - 应该触发 fromAdmin
    FixMessage hb;
    hb.set(tags::MsgType, "0");
    hb.set(tags::SenderCompID, "SERVER");
    hb.set(tags::TargetCompID, "CLIENT");
    hb.set(tags::MsgSeqNum, 2);
    
    session->on_message_received(hb);
    
    REQUIRE(app.from_admin_count == 1);
    REQUIRE(app.from_app_count == 0);  // 不应该调用 fromApp
}

TEST_CASE("Exception safety - Application exceptions are caught", "[application][exception]") {
    ThrowingApplication app;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 模拟收到 Logon 确认 - onLogon 会抛异常，但不应该崩溃
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    
    REQUIRE_NOTHROW(session->on_message_received(logon_ack));
    REQUIRE(session->is_running());  // 会话应该继续运行
}

TEST_CASE("Exception safety - fromApp exception is caught", "[application][exception]") {
    ThrowingApplication app;
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 先建立会话（用 MockApplication 避免 onLogon 异常）
    MockApplication mock_app;
    session->set_application(&mock_app);
    
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    session->on_message_received(logon_ack);
    
    // 切换到会抛异常的 Application
    session->set_application(&app);
    
    // 发送业务消息 - fromApp 会抛异常，但不应该崩溃
    FixMessage order;
    order.set(tags::MsgType, "D");
    order.set(tags::SenderCompID, "SERVER");
    order.set(tags::TargetCompID, "CLIENT");
    order.set(tags::MsgSeqNum, 2);
    
    REQUIRE_NOTHROW(session->on_message_received(order));
    REQUIRE(session->is_running());
}

TEST_CASE("No Application set - business messages are ignored", "[application]") {
    auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
    session->start();
    
    // 先建立会话
    FixMessage logon_ack;
    logon_ack.set(tags::MsgType, "A");
    logon_ack.set(tags::SenderCompID, "SERVER");
    logon_ack.set(tags::TargetCompID, "CLIENT");
    logon_ack.set(tags::MsgSeqNum, 1);
    logon_ack.set(tags::HeartBtInt, 30);
    session->on_message_received(logon_ack);
    
    // 发送业务消息，应该被忽略而不是崩溃
    FixMessage order;
    order.set(tags::MsgType, "D");
    order.set(tags::SenderCompID, "SERVER");
    order.set(tags::TargetCompID, "CLIENT");
    order.set(tags::MsgSeqNum, 2);
    
    REQUIRE_NOTHROW(session->on_message_received(order));
}

TEST_CASE("MatchingEngine basic operations", "[application][engine]") {
    MatchingEngine engine;
    
    SECTION("Start and stop") {
        REQUIRE_FALSE(engine.is_running());
        engine.start();
        REQUIRE(engine.is_running());
        engine.stop();
        REQUIRE_FALSE(engine.is_running());
    }
    
    SECTION("Submit events") {
        engine.start();
        
        SessionID sid("CLIENT", "SERVER");
        FixMessage msg;
        msg.set(tags::MsgType, "D");
        
        // 提交事件不应该阻塞
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::NEW_ORDER, sid, msg}));
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::CANCEL_REQUEST, sid, msg}));
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::SESSION_LOGON, sid}));
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sid}));
        
        // 给引擎一点时间处理
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        engine.stop();
    }
}

TEST_CASE("SimulationApp basic functionality", "[application][simulation]") {
    SimulationApp app;
    app.start();
    
    SessionID sid("SERVER", "CLIENT");
    
    SECTION("onLogon does not throw") {
        REQUIRE_NOTHROW(app.onLogon(sid));
    }
    
    SECTION("onLogout does not throw") {
        REQUIRE_NOTHROW(app.onLogout(sid));
    }
    
    SECTION("fromApp handles NewOrderSingle") {
        FixMessage order;
        order.set(tags::MsgType, "D");
        order.set(11, "ORDER001");
        order.set(55, "AAPL");
        order.set(54, "1");
        order.set(38, "100");
        order.set(44, "150.50");
        order.set(40, "2");
        
        REQUIRE_NOTHROW(app.fromApp(order, sid));
    }
    
    SECTION("fromApp handles OrderCancelRequest") {
        FixMessage cancel;
        cancel.set(tags::MsgType, "F");
        cancel.set(41, "ORDER001");
        cancel.set(11, "CANCEL001");
        cancel.set(55, "AAPL");
        
        REQUIRE_NOTHROW(app.fromApp(cancel, sid));
    }
    
    SECTION("fromApp handles unknown message type") {
        FixMessage unknown;
        unknown.set(tags::MsgType, "Z");
        
        REQUIRE_NOTHROW(app.fromApp(unknown, sid));
    }
    
    SECTION("toApp does not throw") {
        FixMessage msg;
        msg.set(tags::MsgType, "8");
        
        REQUIRE_NOTHROW(app.toApp(msg, sid));
    }
    
    // 给引擎时间处理队列中的事件
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    app.stop();
}

TEST_CASE("Server session with Application - onLogon called", "[application][server]") {
    MockApplication app;
    auto session = std::make_shared<Session>("SERVER", "CLIENT", 30, [](){});
    session->set_application(&app);
    session->start();
    
    // 模拟收到客户端 Logon
    FixMessage client_logon;
    client_logon.set(tags::MsgType, "A");
    client_logon.set(tags::SenderCompID, "CLIENT");
    client_logon.set(tags::TargetCompID, "SERVER");
    client_logon.set(tags::MsgSeqNum, 1);
    client_logon.set(tags::HeartBtInt, 30);
    client_logon.set(tags::EncryptMethod, "0");
    
    session->on_message_received(client_logon);
    
    REQUIRE(app.logon_count == 1);
    REQUIRE(app.last_session_id.senderCompID == "SERVER");
    REQUIRE(app.last_session_id.targetCompID == "CLIENT");
}
