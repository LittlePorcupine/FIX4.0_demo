/**
 * @file simulation_app.cpp
 * @brief 模拟交易应用层实现
 * 
 * 集成AccountManager、PositionManager、InstrumentManager、RiskManager，
 * 实现完整的模拟交易流程：
 * 1. 接收FIX订单消息
 * 2. 风控检查
 * 3. 提交撮合引擎
 * 4. 处理成交回报，更新账户和持仓
 */

#include "app/simulation_app.hpp"
#include "fix/fix_message_builder.hpp"
#include "fix/fix_tags.hpp"
#include "base/logger.hpp"
#include "storage/store.hpp"
#include <cstdlib>

namespace fix40 {

// ============================================================================
// FIX 消息解析辅助函数
// ============================================================================

namespace {

/**
 * @brief 解析结果
 */
struct ParseResult {
    bool success = false;
    std::string error;
    Order order;
};

/**
 * @brief 从 FIX 消息解析 Order 结构
 * @return ParseResult 包含解析结果和错误信息
 */
ParseResult parseNewOrderSingle(const FixMessage& msg, const SessionID& sessionID) {
    ParseResult result;
    Order& order = result.order;
    order.sessionID = sessionID;
    
    // 必填字段：ClOrdID
    if (!msg.has(tags::ClOrdID)) {
        result.error = "Missing required field: ClOrdID(11)";
        return result;
    }
    order.clOrdID = msg.get_string(tags::ClOrdID);
    
    // 必填字段：Symbol
    if (!msg.has(tags::Symbol)) {
        result.error = "Missing required field: Symbol(55)";
        return result;
    }
    order.symbol = msg.get_string(tags::Symbol);
    
    // 必填字段：Side (1=Buy, 2=Sell)
    if (!msg.has(tags::Side)) {
        result.error = "Missing required field: Side(54)";
        return result;
    }
    std::string sideStr = msg.get_string(tags::Side);
    if (sideStr == "1") {
        order.side = OrderSide::BUY;
    } else if (sideStr == "2") {
        order.side = OrderSide::SELL;
    } else {
        result.error = "Invalid Side(54) value: " + sideStr + " (expected 1 or 2)";
        return result;
    }
    
    // 必填字段：OrderQty
    if (!msg.has(tags::OrderQty)) {
        result.error = "Missing required field: OrderQty(38)";
        return result;
    }
    try {
        order.orderQty = std::stoll(msg.get_string(tags::OrderQty));
        if (order.orderQty <= 0) {
            result.error = "Invalid OrderQty(38): must be positive";
            return result;
        }
    } catch (const std::exception& e) {
        result.error = "Invalid OrderQty(38) format: " + std::string(e.what());
        return result;
    }
    
    // 必填字段：OrdType (1=Market, 2=Limit)
    if (!msg.has(tags::OrdType)) {
        result.error = "Missing required field: OrdType(40)";
        return result;
    }
    std::string ordTypeStr = msg.get_string(tags::OrdType);
    if (ordTypeStr == "1") {
        order.ordType = OrderType::MARKET;
    } else if (ordTypeStr == "2") {
        order.ordType = OrderType::LIMIT;
    } else {
        LOG() << "[SimulationApp] Warning: Unknown OrdType(40)=" << ordTypeStr << ", defaulting to LIMIT";
        order.ordType = OrderType::LIMIT;
    }
    
    // Price (限价单必填)
    if (msg.has(tags::Price)) {
        try {
            order.price = std::stod(msg.get_string(tags::Price));
        } catch (const std::exception& e) {
            result.error = "Invalid Price(44) format: " + std::string(e.what());
            return result;
        }
    } else if (order.ordType == OrderType::LIMIT) {
        result.error = "Missing required field: Price(44) for limit order";
        return result;
    }
    
    // TimeInForce: 0=Day, 1=GTC, 3=IOC, 4=FOK (可选，默认 DAY)
    if (msg.has(tags::TimeInForce)) {
        std::string tifStr = msg.get_string(tags::TimeInForce);
        if (tifStr == "0") order.timeInForce = TimeInForce::DAY;
        else if (tifStr == "1") order.timeInForce = TimeInForce::GTC;
        else if (tifStr == "3") order.timeInForce = TimeInForce::IOC;
        else if (tifStr == "4") order.timeInForce = TimeInForce::FOK;
        else {
            LOG() << "[SimulationApp] Warning: Unknown TimeInForce(59)=" << tifStr << ", defaulting to DAY";
            order.timeInForce = TimeInForce::DAY;
        }
    }
    
    // 初始化执行状态
    order.status = OrderStatus::PENDING_NEW;
    order.cumQty = 0;
    order.leavesQty = order.orderQty;
    order.avgPx = 0.0;
    order.createTime = std::chrono::system_clock::now();
    order.updateTime = order.createTime;
    
    result.success = true;
    return result;
}

/**
 * @brief 从 FIX 消息解析 CancelRequest 结构
 */
CancelRequest parseCancelRequest(const FixMessage& msg, const SessionID& sessionID) {
    CancelRequest req;
    req.sessionID = sessionID;
    
    req.clOrdID = msg.get_string(tags::ClOrdID);
    req.origClOrdID = msg.get_string(tags::OrigClOrdID);
    
    if (msg.has(tags::Symbol)) {
        req.symbol = msg.get_string(tags::Symbol);
    }
    
    return req;
}

} // anonymous namespace

// ============================================================================
// SimulationApp 实现
// ============================================================================

SimulationApp::SimulationApp()
    : store_(nullptr) {
    initializeManagers();
}

SimulationApp::SimulationApp(IStore* store)
    : accountManager_(store)
    , positionManager_(store)
    , store_(store) {
    initializeManagers();
}

void SimulationApp::initializeManagers() {
    // 设置撮合引擎与各管理器的关联
    engine_.setRiskManager(&riskManager_);
    engine_.setAccountManager(&accountManager_);
    engine_.setPositionManager(&positionManager_);
    engine_.setInstrumentManager(&instrumentManager_);
    
    // 设置 ExecutionReport 回调
    engine_.setExecutionReportCallback(
        [this](const SessionID& sid, const ExecutionReport& rpt) {
            onExecutionReport(sid, rpt);
        });
}

SimulationApp::~SimulationApp() {
    stop();
}

void SimulationApp::start() {
    engine_.start();
}

void SimulationApp::stop() {
    engine_.stop();
}

void SimulationApp::onLogon(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged on: " << sessionID.to_string();
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGON, sessionID});
}

void SimulationApp::onLogout(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged out: " << sessionID.to_string();
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sessionID});
}

void SimulationApp::fromApp(const FixMessage& msg, const SessionID& sessionID) {
    const std::string msgType = msg.get_string(tags::MsgType);
    
    LOG() << "[SimulationApp] Received business message: MsgType=" << msgType 
          << " from " << sessionID.to_string();
    
    // 解析 FIX 消息，转换为内部结构，提交到队列
    if (msgType == "D") {
        // NewOrderSingle
        ParseResult result = parseNewOrderSingle(msg, sessionID);
        if (!result.success) {
            LOG() << "[SimulationApp] Parse failed: " << result.error;
            // 发送拒绝报告
            ExecutionReport reject;
            reject.clOrdID = msg.has(tags::ClOrdID) ? msg.get_string(tags::ClOrdID) : "";
            reject.symbol = msg.has(tags::Symbol) ? msg.get_string(tags::Symbol) : "";
            reject.ordStatus = OrderStatus::REJECTED;
            reject.execTransType = ExecTransType::NEW;
            reject.ordRejReason = 99;  // 其他原因
            reject.text = result.error;
            reject.transactTime = std::chrono::system_clock::now();
            onExecutionReport(sessionID, reject);
            return;
        }
        
        Order& order = result.order;
        std::string accountId = extractAccountId(sessionID);
        
        // 确保账户存在
        getOrCreateAccount(accountId);
        
        // 获取合约信息
        const Instrument* instrument = instrumentManager_.getInstrument(order.symbol);
        if (!instrument) {
            LOG() << "[SimulationApp] Instrument not found: " << order.symbol;
            ExecutionReport reject;
            reject.clOrdID = order.clOrdID;
            reject.symbol = order.symbol;
            reject.side = order.side;
            reject.ordType = order.ordType;
            reject.orderQty = order.orderQty;
            reject.price = order.price;
            reject.ordStatus = OrderStatus::REJECTED;
            reject.execTransType = ExecTransType::NEW;
            reject.ordRejReason = static_cast<int>(RejectReason::INSTRUMENT_NOT_FOUND);
            reject.text = "Instrument not found: " + order.symbol;
            reject.transactTime = std::chrono::system_clock::now();
            onExecutionReport(sessionID, reject);
            return;
        }
        
        // 获取账户和持仓信息
        auto accountOpt = accountManager_.getAccount(accountId);
        if (!accountOpt) {
            LOG() << "[SimulationApp] Account not found: " << accountId;
            return;
        }
        
        Position position;
        auto posOpt = positionManager_.getPosition(accountId, order.symbol);
        if (posOpt) {
            position = *posOpt;
        } else {
            position.accountId = accountId;
            position.instrumentId = order.symbol;
        }
        
        // 获取行情快照
        MarketDataSnapshot snapshot;
        const MarketDataSnapshot* snapshotPtr = engine_.getMarketSnapshot(order.symbol);
        if (snapshotPtr) {
            snapshot = *snapshotPtr;
        } else {
            snapshot.instrumentId = order.symbol;
            // 使用涨跌停价作为默认值
            snapshot.upperLimitPrice = instrument->upperLimitPrice;
            snapshot.lowerLimitPrice = instrument->lowerLimitPrice;
        }
        
        // 判断开平标志（简化处理：买单开多/平空，卖单开空/平多）
        OffsetFlag offsetFlag = OffsetFlag::OPEN;
        if (order.side == OrderSide::BUY && position.shortPosition > 0) {
            offsetFlag = OffsetFlag::CLOSE;  // 买单平空
        } else if (order.side == OrderSide::SELL && position.longPosition > 0) {
            offsetFlag = OffsetFlag::CLOSE;  // 卖单平多
        }
        
        // 风控检查
        CheckResult checkResult = riskManager_.checkOrder(
            order, *accountOpt, position, *instrument, snapshot, offsetFlag);
        
        if (!checkResult.passed) {
            LOG() << "[SimulationApp] Risk check failed: " << checkResult.rejectText;
            ExecutionReport reject;
            reject.clOrdID = order.clOrdID;
            reject.symbol = order.symbol;
            reject.side = order.side;
            reject.ordType = order.ordType;
            reject.orderQty = order.orderQty;
            reject.price = order.price;
            reject.ordStatus = OrderStatus::REJECTED;
            reject.execTransType = ExecTransType::NEW;
            reject.ordRejReason = static_cast<int>(checkResult.rejectReason);
            reject.text = checkResult.rejectText;
            reject.transactTime = std::chrono::system_clock::now();
            onExecutionReport(sessionID, reject);
            return;
        }
        
        // 开仓订单：冻结保证金
        if (offsetFlag == OffsetFlag::OPEN) {
            double requiredMargin = riskManager_.calculateRequiredMargin(order, *instrument);
            if (!accountManager_.freezeMargin(accountId, requiredMargin)) {
                LOG() << "[SimulationApp] Failed to freeze margin: " << requiredMargin;
                ExecutionReport reject;
                reject.clOrdID = order.clOrdID;
                reject.symbol = order.symbol;
                reject.side = order.side;
                reject.ordType = order.ordType;
                reject.orderQty = order.orderQty;
                reject.price = order.price;
                reject.ordStatus = OrderStatus::REJECTED;
                reject.execTransType = ExecTransType::NEW;
                reject.ordRejReason = static_cast<int>(RejectReason::INSUFFICIENT_FUNDS);
                reject.text = "Failed to freeze margin";
                reject.transactTime = std::chrono::system_clock::now();
                onExecutionReport(sessionID, reject);
                return;
            }
            
            // 记录订单的冻结保证金信息（包含原始总量，用于正确处理部分成交）
            {
                std::lock_guard<std::mutex> lock(mapMutex_);
                orderMarginInfoMap_[order.clOrdID] = OrderMarginInfo(requiredMargin, order.orderQty);
            }
        }
        
        // 记录订单到账户的映射
        {
            std::lock_guard<std::mutex> lock(mapMutex_);
            orderAccountMap_[order.clOrdID] = accountId;
        }
        
        // 提交到撮合引擎
        engine_.submit(OrderEvent::newOrder(order));
        
    } else if (msgType == "F") {
        // OrderCancelRequest
        CancelRequest req = parseCancelRequest(msg, sessionID);
        engine_.submit(OrderEvent::cancelRequest(req));
    } else {
        LOG() << "[SimulationApp] Unhandled message type: " << msgType;
    }
}

void SimulationApp::toApp(FixMessage& msg, const SessionID& sessionID) {
    const std::string msgType = msg.get_string(tags::MsgType);
    LOG() << "[SimulationApp] Sending business message: MsgType=" << msgType
          << " via " << sessionID.to_string();
}

void SimulationApp::onExecutionReport(const SessionID& sessionID, const ExecutionReport& report) {
    LOG() << "[SimulationApp] Sending ExecutionReport to " << sessionID.to_string()
          << " ClOrdID=" << report.clOrdID
          << " OrdStatus=" << static_cast<int>(report.ordStatus);
    
    // 获取账户ID
    std::string accountId;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = orderAccountMap_.find(report.clOrdID);
        if (it != orderAccountMap_.end()) {
            accountId = it->second;
        } else {
            accountId = extractAccountId(sessionID);
        }
    }
    
    // 根据订单状态处理账户和持仓更新
    switch (report.ordStatus) {
        case OrderStatus::FILLED:
        case OrderStatus::PARTIALLY_FILLED:
            if (report.lastShares > 0) {
                handleFill(accountId, report);
            }
            break;
        case OrderStatus::REJECTED:
            handleReject(accountId, report);
            break;
        case OrderStatus::CANCELED:
            handleCancel(accountId, report);
            break;
        default:
            break;
    }
    
    // 清理已完成订单的映射
    if (report.ordStatus == OrderStatus::FILLED ||
        report.ordStatus == OrderStatus::REJECTED ||
        report.ordStatus == OrderStatus::CANCELED) {
        std::lock_guard<std::mutex> lock(mapMutex_);
        orderAccountMap_.erase(report.clOrdID);
        orderMarginInfoMap_.erase(report.clOrdID);
    }
    
    // 将 ExecutionReport 转换为 FIX 消息
    FixMessage msg = buildExecutionReport(report);
    
    // 通过 SessionManager 发送
    if (!sessionManager_.sendMessage(sessionID, msg)) {
        LOG() << "[SimulationApp] Failed to send ExecutionReport: session not found "
              << sessionID.to_string();
    }
}

void SimulationApp::handleFill(const std::string& accountId, const ExecutionReport& report) {
    // 获取合约信息
    const Instrument* instrument = instrumentManager_.getInstrument(report.symbol);
    if (!instrument) {
        LOG() << "[SimulationApp] handleFill: Instrument not found: " << report.symbol;
        return;
    }
    
    // 获取持仓信息
    Position position;
    auto posOpt = positionManager_.getPosition(accountId, report.symbol);
    if (posOpt) {
        position = *posOpt;
    } else {
        position.accountId = accountId;
        position.instrumentId = report.symbol;
    }
    
    // 判断开平标志
    bool isClose = false;
    if (report.side == OrderSide::BUY && position.shortPosition > 0) {
        isClose = true;  // 买单平空
    } else if (report.side == OrderSide::SELL && position.longPosition > 0) {
        isClose = true;  // 卖单平多
    }
    
    if (isClose) {
        // 平仓处理
        double closeProfit = positionManager_.closePosition(
            accountId, report.symbol, report.side,
            report.lastShares, report.lastPx, instrument->volumeMultiple);
        
        // 计算释放的保证金
        double releasedMargin = 0.0;
        if (report.side == OrderSide::BUY) {
            // 平空头，释放空头保证金
            releasedMargin = report.lastPx * report.lastShares * 
                             instrument->volumeMultiple * instrument->marginRate;
        } else {
            // 平多头，释放多头保证金
            releasedMargin = report.lastPx * report.lastShares * 
                             instrument->volumeMultiple * instrument->marginRate;
        }
        
        // 释放占用保证金
        accountManager_.releaseMargin(accountId, releasedMargin);
        
        // 记录平仓盈亏
        accountManager_.addCloseProfit(accountId, closeProfit);
        
        LOG() << "[SimulationApp] Close position: " << report.symbol
              << " side=" << static_cast<int>(report.side)
              << " qty=" << report.lastShares
              << " price=" << report.lastPx
              << " profit=" << closeProfit;
    } else {
        // 开仓处理
        double margin = instrument->calculateMargin(report.lastPx, report.lastShares);
        
        // 获取冻结的保证金（使用原始总量计算，避免部分成交累计误差）
        double frozenMargin = 0.0;
        {
            std::lock_guard<std::mutex> lock(mapMutex_);
            auto it = orderMarginInfoMap_.find(report.clOrdID);
            if (it != orderMarginInfoMap_.end()) {
                // 使用原始总冻结保证金按比例计算，避免累计误差
                frozenMargin = it->second.calculateReleaseAmount(report.lastShares);
            }
        }
        
        // 冻结转占用
        accountManager_.confirmMargin(accountId, frozenMargin, margin);
        
        // 开仓
        positionManager_.openPosition(
            accountId, report.symbol, report.side,
            report.lastShares, report.lastPx, margin);
        
        LOG() << "[SimulationApp] Open position: " << report.symbol
              << " side=" << static_cast<int>(report.side)
              << " qty=" << report.lastShares
              << " price=" << report.lastPx
              << " margin=" << margin
              << " frozenReleased=" << frozenMargin;
    }
}

void SimulationApp::handleReject(const std::string& accountId, const ExecutionReport& report) {
    // 释放全部冻结的保证金（拒绝时释放原始总冻结金额）
    double frozenMargin = 0.0;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = orderMarginInfoMap_.find(report.clOrdID);
        if (it != orderMarginInfoMap_.end()) {
            // 拒绝时释放全部原始冻结保证金
            frozenMargin = it->second.originalFrozenMargin;
        }
    }
    
    if (frozenMargin > 0) {
        accountManager_.unfreezeMargin(accountId, frozenMargin);
        LOG() << "[SimulationApp] Released frozen margin on reject: " << frozenMargin;
    }
}

void SimulationApp::handleCancel(const std::string& accountId, const ExecutionReport& report) {
    // 释放剩余未释放的冻结保证金（撤单时只释放未成交部分）
    double frozenMargin = 0.0;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = orderMarginInfoMap_.find(report.clOrdID);
        if (it != orderMarginInfoMap_.end()) {
            // 撤单时释放剩余未释放的冻结保证金
            frozenMargin = it->second.getRemainingFrozen();
        }
    }
    
    if (frozenMargin > 0) {
        accountManager_.unfreezeMargin(accountId, frozenMargin);
        LOG() << "[SimulationApp] Released frozen margin on cancel: " << frozenMargin;
    }
}

std::string SimulationApp::extractAccountId(const SessionID& sessionID) const {
    // 使用SenderCompID作为账户ID
    return sessionID.senderCompID;
}

Account SimulationApp::getOrCreateAccount(const std::string& accountId, double initialBalance) {
    auto accountOpt = accountManager_.getAccount(accountId);
    if (accountOpt) {
        return *accountOpt;
    }
    return accountManager_.createAccount(accountId, initialBalance);
}

} // namespace fix40
