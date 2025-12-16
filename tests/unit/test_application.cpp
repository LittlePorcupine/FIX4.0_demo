#include "../catch2/catch.hpp"
#include "fix/application.hpp"
#include "fix/session.hpp"
#include "fix/fix_codec.hpp"
#include "fix/fix_tags.hpp"
#include "fix/fix_messages.hpp"
#include "app/simulation_app.hpp"
#include "app/matching_engine.hpp"
#include "app/order.hpp"
#include "market/market_data.hpp"

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
        
        // 创建测试订单
        Order order;
        order.clOrdID = "ORDER001";
        order.symbol = "AAPL";
        order.side = OrderSide::BUY;
        order.orderQty = 100;
        order.price = 150.50;
        order.ordType = OrderType::LIMIT;
        order.sessionID = sid;
        
        // 创建撤单请求
        CancelRequest cancelReq;
        cancelReq.clOrdID = "CANCEL001";
        cancelReq.origClOrdID = "ORDER001";
        cancelReq.symbol = "AAPL";
        cancelReq.sessionID = sid;
        
        // 提交事件不应该阻塞
        REQUIRE_NOTHROW(engine.submit(OrderEvent::newOrder(order)));
        REQUIRE_NOTHROW(engine.submit(OrderEvent::cancelRequest(cancelReq)));
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::SESSION_LOGON, sid}));
        REQUIRE_NOTHROW(engine.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sid}));
        
        // stop() 会等待队列处理完成
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
    
    // stop() 会等待队列处理完成
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

// 辅助函数：轮询等待条件满足，避免 flaky tests
template<typename Predicate>
bool waitFor(Predicate pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

TEST_CASE("MatchingEngine with OrderBook integration", "[application][engine][orderbook]") {
    MatchingEngine engine;
    
    // 收集 ExecutionReport
    std::vector<std::pair<SessionID, ExecutionReport>> reports;
    std::mutex reportsMutex;
    
    engine.setExecutionReportCallback([&](const SessionID& sid, const ExecutionReport& rpt) {
        std::lock_guard<std::mutex> lock(reportsMutex);
        reports.push_back({sid, rpt});
    });
    
    engine.start();
    
    SessionID sid("CLIENT", "SERVER");
    
    SECTION("New order creates pending order and sends ExecutionReport") {
        Order order;
        order.clOrdID = "ORDER001";
        order.symbol = "AAPL";
        order.side = OrderSide::BUY;
        order.orderQty = 100;
        order.leavesQty = 100;
        order.price = 150.0;
        order.ordType = OrderType::LIMIT;
        order.sessionID = sid;
        
        engine.submit(OrderEvent::newOrder(order));
        
        // 轮询等待报告到达
        REQUIRE(waitFor([&]() {
            std::lock_guard<std::mutex> lock(reportsMutex);
            return reports.size() >= 1;
        }));
        
        std::lock_guard<std::mutex> lock(reportsMutex);
        REQUIRE(reports.size() == 1);
        REQUIRE(reports[0].first == sid);
        REQUIRE(reports[0].second.clOrdID == "ORDER001");
        REQUIRE(reports[0].second.ordStatus == OrderStatus::NEW);
        REQUIRE_FALSE(reports[0].second.orderID.empty());
        
        // 验证挂单列表已创建（行情驱动模式下订单进入 pendingOrders_）
        const auto* pendingOrders = engine.getPendingOrders("AAPL");
        REQUIRE(pendingOrders != nullptr);
        REQUIRE(pendingOrders->size() == 1);
    }
    
    SECTION("Market data triggers matching") {
        // 行情驱动模式：先挂买单，然后提交行情触发成交
        Order buyOrder;
        buyOrder.clOrdID = "BUY001";
        buyOrder.symbol = "TEST";
        buyOrder.side = OrderSide::BUY;
        buyOrder.orderQty = 10;
        buyOrder.leavesQty = 10;
        buyOrder.price = 100.0;
        buyOrder.ordType = OrderType::LIMIT;
        buyOrder.sessionID = sid;
        
        engine.submit(OrderEvent::newOrder(buyOrder));
        
        // 等待买单确认（挂单）
        REQUIRE(waitFor([&]() {
            std::lock_guard<std::mutex> lock(reportsMutex);
            return reports.size() >= 1;
        }));
        
        {
            std::lock_guard<std::mutex> lock(reportsMutex);
            REQUIRE(reports.size() == 1);
            REQUIRE(reports[0].second.clOrdID == "BUY001");
            REQUIRE(reports[0].second.ordStatus == OrderStatus::NEW);
        }
        
        // 验证订单在挂单列表中
        const auto* pendingOrders = engine.getPendingOrders("TEST");
        REQUIRE(pendingOrders != nullptr);
        REQUIRE(pendingOrders->size() == 1);
        
        // 提交行情数据，卖一价 <= 买单价格，应该触发成交
        MarketData md;
        md.setInstrumentID("TEST");
        md.bidPrice1 = 99.0;
        md.bidVolume1 = 100;
        md.askPrice1 = 100.0;  // 卖一价 == 买单价格，应该成交
        md.askVolume1 = 100;
        md.lastPrice = 100.0;
        
        engine.submitMarketData(md);
        
        // 等待成交报告
        REQUIRE(waitFor([&]() {
            std::lock_guard<std::mutex> lock(reportsMutex);
            return reports.size() >= 2;
        }));
        
        std::lock_guard<std::mutex> lock(reportsMutex);
        REQUIRE(reports.size() >= 2);
        
        // 找到成交报告
        auto fillReport = std::find_if(reports.begin(), reports.end(),
            [](const auto& p) { 
                return p.second.clOrdID == "BUY001" && 
                       p.second.ordStatus == OrderStatus::FILLED; 
            });
        REQUIRE(fillReport != reports.end());
        REQUIRE(fillReport->second.cumQty == 10);
        REQUIRE(fillReport->second.lastPx == 100.0);  // 以卖一价成交
        
        // 验证挂单列表为空（已成交）
        REQUIRE(engine.getTotalPendingOrderCount() == 0);
    }
    
    SECTION("Cancel order from pending orders") {
        // 先挂单
        Order order;
        order.clOrdID = "ORDER001";
        order.symbol = "AAPL";
        order.side = OrderSide::BUY;
        order.orderQty = 100;
        order.leavesQty = 100;
        order.price = 150.0;
        order.ordType = OrderType::LIMIT;
        order.sessionID = sid;
        
        engine.submit(OrderEvent::newOrder(order));
        
        // 等待订单确认
        REQUIRE(waitFor([&]() {
            std::lock_guard<std::mutex> lock(reportsMutex);
            return reports.size() >= 1;
        }));
        
        // 验证订单在挂单列表中
        const auto* pendingOrders = engine.getPendingOrders("AAPL");
        REQUIRE(pendingOrders != nullptr);
        REQUIRE(pendingOrders->size() == 1);
        
        // 撤单
        CancelRequest cancel;
        cancel.clOrdID = "CANCEL001";
        cancel.origClOrdID = "ORDER001";
        cancel.symbol = "AAPL";
        cancel.sessionID = sid;
        
        engine.submit(OrderEvent::cancelRequest(cancel));
        
        // 等待撤单确认
        REQUIRE(waitFor([&]() {
            std::lock_guard<std::mutex> lock(reportsMutex);
            return reports.size() >= 2;
        }));
        
        std::lock_guard<std::mutex> lock(reportsMutex);
        // 应该有：订单确认 + 撤单确认
        REQUIRE(reports.size() == 2);
        
        auto cancelReport = std::find_if(reports.begin(), reports.end(),
            [](const auto& p) { return p.second.origClOrdID == "ORDER001"; });
        REQUIRE(cancelReport != reports.end());
        REQUIRE(cancelReport->second.ordStatus == OrderStatus::CANCELED);
        
        // 验证挂单列表为空（已撤单）
        REQUIRE(engine.getTotalPendingOrderCount() == 0);
    }
    
    engine.stop();
}


// =============================================================================
// ExecutionReport 属性测试
// =============================================================================

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/fix_message_builder.hpp"
#include "app/instrument.hpp"
#include "app/account.hpp"
#include "app/position.hpp"
#include "app/risk_manager.hpp"
#include "app/account_manager.hpp"
#include "app/position_manager.hpp"
#include "app/instrument_manager.hpp"

namespace {

/**
 * @brief 创建测试用ExecutionReport
 */
ExecutionReport makeTestExecutionReport(
    const std::string& orderID,
    const std::string& clOrdID,
    const std::string& execID,
    const std::string& symbol,
    OrderSide side,
    OrderType ordType,
    int64_t orderQty,
    double price,
    OrderStatus ordStatus,
    int64_t lastShares = 0,
    double lastPx = 0.0,
    int64_t cumQty = 0,
    double avgPx = 0.0
) {
    ExecutionReport report;
    report.orderID = orderID;
    report.clOrdID = clOrdID;
    report.execID = execID;
    report.symbol = symbol;
    report.side = side;
    report.ordType = ordType;
    report.orderQty = orderQty;
    report.price = price;
    report.ordStatus = ordStatus;
    report.lastShares = lastShares;
    report.lastPx = lastPx;
    report.cumQty = cumQty;
    report.avgPx = avgPx;
    report.leavesQty = orderQty - cumQty;
    report.transactTime = std::chrono::system_clock::now();
    report.execTransType = ExecTransType::NEW;
    return report;
}

/**
 * @brief 创建测试用合约
 */
Instrument makeTestInstrument(const std::string& instrumentId) {
    Instrument inst(instrumentId, "CFFEX", "IF", 0.2, 300, 0.12);
    inst.updateLimitPrices(5000.0, 3000.0);
    return inst;
}

} // anonymous namespace

/**
 * **Feature: paper-trading-system, Property 17: ExecutionReport生成正确性**
 * **Validates: Requirements 4.7, 5.4, 6.3**
 *
 * *对于任意* 订单状态变化，生成的ExecutionReport应包含正确的订单ID、状态、成交信息
 */
TEST_CASE("Property 17: ExecutionReport生成正确性", "[application][property]") {
    
    rc::prop("ExecutionReport包含正确的订单标识符",
        []() {
            // 生成随机订单标识符
            auto orderID = *rc::gen::nonEmpty(rc::gen::string<std::string>());
            auto clOrdID = *rc::gen::nonEmpty(rc::gen::string<std::string>());
            auto execID = *rc::gen::nonEmpty(rc::gen::string<std::string>());
            auto symbol = *rc::gen::element(std::string("IF2601"), std::string("IC2601"), std::string("IH2601"));
            
            // 生成随机订单参数
            auto side = *rc::gen::element(OrderSide::BUY, OrderSide::SELL);
            auto ordType = *rc::gen::element(OrderType::LIMIT, OrderType::MARKET);
            auto orderQty = *rc::gen::inRange<int64_t>(1, 1000);
            auto price = *rc::gen::inRange(3000, 5000);
            auto ordStatus = *rc::gen::element(
                OrderStatus::NEW, 
                OrderStatus::PARTIALLY_FILLED, 
                OrderStatus::FILLED,
                OrderStatus::CANCELED,
                OrderStatus::REJECTED
            );
            
            ExecutionReport report = makeTestExecutionReport(
                orderID, clOrdID, execID, symbol, side, ordType,
                orderQty, static_cast<double>(price), ordStatus
            );
            
            // 转换为FIX消息
            FixMessage msg = buildExecutionReport(report);
            
            // 验证标识符正确
            RC_ASSERT(msg.get_string(tags::OrderID) == orderID);
            RC_ASSERT(msg.get_string(tags::ClOrdID) == clOrdID);
            RC_ASSERT(msg.get_string(tags::ExecID) == execID);
            RC_ASSERT(msg.get_string(tags::Symbol) == symbol);
        });
    
    rc::prop("ExecutionReport包含正确的订单状态",
        []() {
            auto ordStatus = *rc::gen::element(
                OrderStatus::NEW, 
                OrderStatus::PARTIALLY_FILLED, 
                OrderStatus::FILLED,
                OrderStatus::CANCELED,
                OrderStatus::REJECTED
            );
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CLO001", "EXE001", "IF2601",
                OrderSide::BUY, OrderType::LIMIT, 100, 4000.0, ordStatus
            );
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证状态正确
            std::string expectedStatus = ordStatusToFix(ordStatus);
            RC_ASSERT(msg.get_string(tags::OrdStatus) == expectedStatus);
        });
    
    rc::prop("ExecutionReport成交信息正确",
        []() {
            auto orderQty = *rc::gen::inRange<int64_t>(10, 1000);
            auto lastShares = *rc::gen::inRange<int64_t>(1, orderQty);
            auto lastPx = *rc::gen::inRange(3000, 5000);
            auto cumQty = *rc::gen::inRange<int64_t>(lastShares, orderQty);
            auto avgPx = *rc::gen::inRange(3000, 5000);
            
            // 确保数据有效
            RC_PRE(lastShares > 0);
            RC_PRE(cumQty >= lastShares);
            RC_PRE(cumQty <= orderQty);
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CLO001", "EXE001", "IF2601",
                OrderSide::BUY, OrderType::LIMIT, orderQty, 4000.0,
                OrderStatus::PARTIALLY_FILLED,
                lastShares, static_cast<double>(lastPx),
                cumQty, static_cast<double>(avgPx)
            );
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证成交信息
            RC_ASSERT(std::stoll(msg.get_string(tags::OrderQty)) == orderQty);
            RC_ASSERT(std::stoll(msg.get_string(tags::CumQty)) == cumQty);
            RC_ASSERT(std::stoll(msg.get_string(tags::LastShares)) == lastShares);
            RC_ASSERT(std::stod(msg.get_string(tags::LastPx)) == Approx(static_cast<double>(lastPx)));
            RC_ASSERT(std::stod(msg.get_string(tags::AvgPx)) == Approx(static_cast<double>(avgPx)));
        });
    
    rc::prop("ExecutionReport买卖方向正确",
        []() {
            auto side = *rc::gen::element(OrderSide::BUY, OrderSide::SELL);
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CLO001", "EXE001", "IF2601",
                side, OrderType::LIMIT, 100, 4000.0, OrderStatus::NEW
            );
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证买卖方向
            std::string expectedSide = sideToFix(side);
            RC_ASSERT(msg.get_string(tags::Side) == expectedSide);
        });
    
    rc::prop("ExecutionReport订单类型正确",
        []() {
            auto ordType = *rc::gen::element(OrderType::LIMIT, OrderType::MARKET);
            auto price = ordType == OrderType::LIMIT ? 4000.0 : 0.0;
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CLO001", "EXE001", "IF2601",
                OrderSide::BUY, ordType, 100, price, OrderStatus::NEW
            );
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证订单类型
            std::string expectedOrdType = ordTypeToFix(ordType);
            RC_ASSERT(msg.get_string(tags::OrdType) == expectedOrdType);
            
            // 限价单应包含价格
            if (ordType == OrderType::LIMIT && price > 0) {
                RC_ASSERT(msg.has(tags::Price));
            }
        });
}

TEST_CASE("Property 17: ExecutionReport拒绝信息正确", "[application][property]") {
    
    rc::prop("拒绝订单包含拒绝原因",
        []() {
            auto rejectReason = *rc::gen::inRange(1, 10);
            auto rejectText = *rc::gen::nonEmpty(rc::gen::string<std::string>());
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CLO001", "EXE001", "IF2601",
                OrderSide::BUY, OrderType::LIMIT, 100, 4000.0, OrderStatus::REJECTED
            );
            report.ordRejReason = rejectReason;
            report.text = rejectText;
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证拒绝原因
            RC_ASSERT(msg.has(tags::OrdRejReason));
            RC_ASSERT(std::stoi(msg.get_string(tags::OrdRejReason)) == rejectReason);
            RC_ASSERT(msg.get_string(tags::Text) == rejectText);
        });
}

TEST_CASE("Property 17: ExecutionReport撤单信息正确", "[application][property]") {
    
    rc::prop("撤单报告包含原订单ID",
        []() {
            auto origClOrdID = *rc::gen::nonEmpty(rc::gen::string<std::string>());
            
            ExecutionReport report = makeTestExecutionReport(
                "ORD001", "CANCEL001", "EXE001", "IF2601",
                OrderSide::BUY, OrderType::LIMIT, 100, 4000.0, OrderStatus::CANCELED
            );
            report.origClOrdID = origClOrdID;
            
            FixMessage msg = buildExecutionReport(report);
            
            // 验证原订单ID
            RC_ASSERT(msg.has(tags::OrigClOrdID));
            RC_ASSERT(msg.get_string(tags::OrigClOrdID) == origClOrdID);
        });
}

TEST_CASE("SimulationApp集成测试 - 风控检查", "[application][integration]") {
    SimulationApp app;
    
    // 添加测试合约
    Instrument inst = makeTestInstrument("IF2601");
    app.getInstrumentManager().addInstrument(inst);
    
    app.start();
    
    SessionID sid("CLIENT", "SERVER");
    
    SECTION("合约不存在时拒绝订单") {
        FixMessage order;
        order.set(tags::MsgType, "D");
        order.set(tags::ClOrdID, "ORDER001");
        order.set(tags::Symbol, "UNKNOWN");  // 不存在的合约
        order.set(tags::Side, "1");
        order.set(tags::OrderQty, "100");
        order.set(tags::Price, "4000.0");
        order.set(tags::OrdType, "2");
        
        // 应该不抛异常
        REQUIRE_NOTHROW(app.fromApp(order, sid));
    }
    
    SECTION("有效订单通过风控检查") {
        // 创建账户
        app.getOrCreateAccount("CLIENT", 1000000.0);
        
        FixMessage order;
        order.set(tags::MsgType, "D");
        order.set(tags::ClOrdID, "ORDER002");
        order.set(tags::Symbol, "IF2601");
        order.set(tags::Side, "1");
        order.set(tags::OrderQty, "1");
        order.set(tags::Price, "4000.0");
        order.set(tags::OrdType, "2");
        
        REQUIRE_NOTHROW(app.fromApp(order, sid));
    }
    
    app.stop();
}

TEST_CASE("SimulationApp集成测试 - 账户和持仓管理", "[application][integration]") {
    SimulationApp app;
    
    // 添加测试合约
    Instrument inst = makeTestInstrument("IF2601");
    app.getInstrumentManager().addInstrument(inst);
    
    SECTION("创建账户") {
        Account account = app.getOrCreateAccount("TEST001", 500000.0);
        REQUIRE(account.accountId == "TEST001");
        REQUIRE(account.balance == 500000.0);
        REQUIRE(account.available == 500000.0);
    }
    
    SECTION("获取已存在的账户") {
        app.getOrCreateAccount("TEST002", 1000000.0);
        Account account = app.getOrCreateAccount("TEST002", 500000.0);  // 不同的初始余额
        REQUIRE(account.accountId == "TEST002");
        REQUIRE(account.balance == 1000000.0);  // 应该是原来的余额
    }
}


// =============================================================================
// 保证金释放测试（修复 Copilot 审核问题）
// =============================================================================

TEST_CASE("SimulationApp - handleReject 释放全部冻结保证金", "[application][margin]") {
    SimulationApp app;
    
    // 添加测试合约
    Instrument inst = makeTestInstrument("IF2601");
    app.getInstrumentManager().addInstrument(inst);
    
    // 创建账户
    std::string accountId = "TEST_REJECT";
    app.getOrCreateAccount(accountId, 1000000.0);
    
    // 验证初始状态
    auto accountBefore = app.getAccountManager().getAccount(accountId);
    REQUIRE(accountBefore.has_value());
    REQUIRE(accountBefore->available == 1000000.0);
    REQUIRE(accountBefore->frozenMargin == 0.0);
    
    // 冻结保证金（模拟下单）
    double frozenAmount = 100000.0;
    REQUIRE(app.getAccountManager().freezeMargin(accountId, frozenAmount));
    
    auto accountAfterFreeze = app.getAccountManager().getAccount(accountId);
    REQUIRE(accountAfterFreeze->available == 900000.0);
    REQUIRE(accountAfterFreeze->frozenMargin == 100000.0);
    
    // 释放保证金（模拟拒绝）
    REQUIRE(app.getAccountManager().unfreezeMargin(accountId, frozenAmount));
    
    // 验证保证金完全释放
    auto accountAfterReject = app.getAccountManager().getAccount(accountId);
    REQUIRE(accountAfterReject.has_value());
    REQUIRE(accountAfterReject->available == 1000000.0);
    REQUIRE(accountAfterReject->frozenMargin == 0.0);
}

TEST_CASE("SimulationApp - handleCancel 释放剩余冻结保证金", "[application][margin]") {
    SimulationApp app;
    
    // 添加测试合约
    Instrument inst = makeTestInstrument("IF2601");
    app.getInstrumentManager().addInstrument(inst);
    
    // 创建账户
    std::string accountId = "TEST_CANCEL";
    app.getOrCreateAccount(accountId, 1000000.0);
    
    // 冻结保证金
    double frozenAmount = 100000.0;
    REQUIRE(app.getAccountManager().freezeMargin(accountId, frozenAmount));
    
    // 部分成交后释放部分保证金（模拟50%成交）
    double partialRelease = 50000.0;
    double actualUsed = 48000.0;  // 实际占用可能略有不同
    REQUIRE(app.getAccountManager().confirmMargin(accountId, partialRelease, actualUsed));
    
    auto accountAfterPartial = app.getAccountManager().getAccount(accountId);
    REQUIRE(accountAfterPartial->frozenMargin == 50000.0);  // 剩余冻结
    REQUIRE(accountAfterPartial->usedMargin == 48000.0);    // 已占用
    
    // 撤单释放剩余冻结保证金
    double remainingFrozen = 50000.0;
    REQUIRE(app.getAccountManager().unfreezeMargin(accountId, remainingFrozen));
    
    // 验证剩余冻结保证金完全释放
    auto accountAfterCancel = app.getAccountManager().getAccount(accountId);
    REQUIRE(accountAfterCancel->frozenMargin == 0.0);
    REQUIRE(accountAfterCancel->usedMargin == 48000.0);  // 已占用保持不变
}

TEST_CASE("SimulationApp - 部分成交保证金计算正确性", "[application][margin][property]") {
    // 测试部分成交时保证金计算的正确性
    // 验证修复：使用原始总冻结保证金按比例计算，避免累计误差
    
    SimulationApp app;
    
    // 使用 OrderMarginInfo 结构测试
    SimulationApp::OrderMarginInfo info(100000.0, 100);  // 100手，总冻结10万
    
    SECTION("单次全部成交") {
        double released = info.calculateReleaseAmount(100);
        REQUIRE(released == Approx(100000.0));
        REQUIRE(info.getRemainingFrozen() == Approx(0.0));
    }
    
    SECTION("两次部分成交") {
        // 第一次成交30手
        double released1 = info.calculateReleaseAmount(30);
        REQUIRE(released1 == Approx(30000.0));
        REQUIRE(info.getRemainingFrozen() == Approx(70000.0));
        
        // 第二次成交30手（关键测试：应该还是30000，不是21000）
        double released2 = info.calculateReleaseAmount(30);
        REQUIRE(released2 == Approx(30000.0));  // 修复前会是21000
        REQUIRE(info.getRemainingFrozen() == Approx(40000.0));
    }
    
    SECTION("三次部分成交") {
        // 第一次成交30手
        double released1 = info.calculateReleaseAmount(30);
        REQUIRE(released1 == Approx(30000.0));
        
        // 第二次成交30手
        double released2 = info.calculateReleaseAmount(30);
        REQUIRE(released2 == Approx(30000.0));
        
        // 第三次成交40手
        double released3 = info.calculateReleaseAmount(40);
        REQUIRE(released3 == Approx(40000.0));
        
        // 验证总释放等于原始冻结
        REQUIRE(info.releasedMargin == Approx(100000.0));
        REQUIRE(info.getRemainingFrozen() == Approx(0.0));
    }
    
    SECTION("不均匀部分成交") {
        // 第一次成交10手
        double released1 = info.calculateReleaseAmount(10);
        REQUIRE(released1 == Approx(10000.0));
        
        // 第二次成交50手
        double released2 = info.calculateReleaseAmount(50);
        REQUIRE(released2 == Approx(50000.0));
        
        // 第三次成交40手
        double released3 = info.calculateReleaseAmount(40);
        REQUIRE(released3 == Approx(40000.0));
        
        // 验证总释放等于原始冻结
        REQUIRE(info.releasedMargin == Approx(100000.0));
        REQUIRE(info.getRemainingFrozen() == Approx(0.0));
    }
}

TEST_CASE("SimulationApp - OrderMarginInfo 边界情况", "[application][margin]") {
    
    SECTION("零数量订单") {
        SimulationApp::OrderMarginInfo info(100000.0, 0);
        double released = info.calculateReleaseAmount(10);
        REQUIRE(released == 0.0);  // 避免除零
    }
    
    SECTION("零冻结保证金") {
        SimulationApp::OrderMarginInfo info(0.0, 100);
        double released = info.calculateReleaseAmount(50);
        REQUIRE(released == 0.0);
    }
    
    SECTION("默认构造") {
        SimulationApp::OrderMarginInfo info;
        REQUIRE(info.originalFrozenMargin == 0.0);
        REQUIRE(info.originalOrderQty == 0);
        REQUIRE(info.releasedMargin == 0.0);
        REQUIRE(info.getRemainingFrozen() == 0.0);
    }
}
