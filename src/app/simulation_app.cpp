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
#include <sstream>
#include <iomanip>
#include <set>
#include <optional>

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
    // 撮合引擎只负责撮合与订单状态推进；账户/持仓/风控等业务权威逻辑由 SimulationApp 负责。
    // 撮合引擎仅需要合约信息用于行情驱动撮合相关的辅助更新（如涨跌停价格）。
    engine_.setInstrumentManager(&instrumentManager_);
    
    // 设置 ExecutionReport 回调
    engine_.setExecutionReportCallback(
        [this](const SessionID& sid, const ExecutionReport& rpt) {
            onExecutionReport(sid, rpt);
        });
    
    // 设置行情更新回调（用于账户价值重算和推送）
    engine_.setMarketDataUpdateCallback(
        [this](const std::string& instrumentId, double lastPrice) {
            onMarketDataUpdate(instrumentId, lastPrice);
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
    // 身份绑定：使用 SenderCompID 作为用户ID
    std::string userId = extractAccountId(sessionID);
    
    // 安全检查：拒绝无效的用户ID
    if (userId.empty()) {
        LOG() << "[SimulationApp] Rejecting logon: invalid user ID for session "
              << sessionID.to_string();
        return;
    }
    
    LOG() << "[SimulationApp] Session logged on: " << sessionID.to_string()
          << " -> User: " << userId;
    
    // 自动开户：如果该用户不存在，初始化一个带初始资金的账户
    if (!accountManager_.hasAccount(userId)) {
        LOG() << "[SimulationApp] Initializing new account for user: " << userId;
        accountManager_.createAccount(userId, 1000000.0);  // 默认 100 万
    }
    
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGON, sessionID});
}

void SimulationApp::onLogout(const SessionID& sessionID) {
    LOG() << "[SimulationApp] Session logged out: " << sessionID.to_string();
    engine_.submit(OrderEvent{OrderEventType::SESSION_LOGOUT, sessionID});
}

void SimulationApp::fromApp(const FixMessage& msg, const SessionID& sessionID) {
    const std::string msgType = msg.get_string(tags::MsgType);
    
    // =========================================================================
    // 1. 身份验证：从 Session 提取用户ID（安全路由的核心）
    // =========================================================================
    // 关键：不从消息体读取 Account 字段，而是使用 Session 绑定的身份
    std::string userId = extractAccountId(sessionID);
    
    LOG() << "[SimulationApp] Received MsgType=" << msgType 
          << " from " << sessionID.to_string()
          << " (User: " << userId << ")";
    
    // =========================================================================
    // 2. 消息路由分发
    // =========================================================================
    if (msgType == "D") {
        // NewOrderSingle - 新订单
        handleNewOrderSingle(msg, sessionID, userId);
    } 
    else if (msgType == "F") {
        // OrderCancelRequest - 撤单请求
        handleOrderCancelRequest(msg, sessionID, userId);
    }
    else if (msgType == "U1") {
        // BalanceQueryRequest - 资金查询（自定义）
        handleBalanceQuery(msg, sessionID, userId);
    }
    else if (msgType == "U3") {
        // PositionQueryRequest - 持仓查询（自定义）
        handlePositionQuery(msg, sessionID, userId);
    }
    else if (msgType == "U7") {
        // InstrumentSearchRequest - 合约搜索（自定义）
        // 注意：合约搜索不需要用户身份验证
        handleInstrumentSearch(msg, sessionID);
    }
    else {
        // 未知消息类型
        LOG() << "[SimulationApp] Unknown message type: " << msgType;
        sendBusinessReject(sessionID, msgType, "Unsupported message type");
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
    
    // 计算可平仓数量和开仓数量
    int64_t closeQty = 0;
    int64_t openQty = report.lastShares;
    
    if (report.side == OrderSide::BUY && position.shortPosition > 0) {
        // 买单：优先平空仓
        closeQty = std::min(report.lastShares, position.shortPosition);
        openQty = report.lastShares - closeQty;
    } else if (report.side == OrderSide::SELL && position.longPosition > 0) {
        // 卖单：优先平多仓
        closeQty = std::min(report.lastShares, position.longPosition);
        openQty = report.lastShares - closeQty;
    }
    
    // 1. 先处理平仓部分
    if (closeQty > 0) {
        double closeProfit = positionManager_.closePosition(
            accountId, report.symbol, report.side,
            closeQty, report.lastPx, instrument->volumeMultiple);
        
        // 计算释放的保证金
        double releasedMargin = report.lastPx * closeQty * 
                                instrument->volumeMultiple * instrument->marginRate;
        
        // 释放占用保证金
        accountManager_.releaseMargin(accountId, releasedMargin);
        
        // 记录平仓盈亏
        accountManager_.addCloseProfit(accountId, closeProfit);
        
        LOG() << "[SimulationApp] Close position: " << report.symbol
              << " side=" << static_cast<int>(report.side)
              << " qty=" << closeQty
              << " price=" << report.lastPx
              << " profit=" << closeProfit;
    }
    
    // 2. 再处理开仓部分
    if (openQty > 0) {
        double margin = instrument->calculateMargin(report.lastPx, openQty);
        
        // 获取冻结的保证金（使用原始总量计算，避免部分成交累计误差）
        double frozenMargin = 0.0;
        {
            std::lock_guard<std::mutex> lock(mapMutex_);
            auto it = orderMarginInfoMap_.find(report.clOrdID);
            if (it != orderMarginInfoMap_.end()) {
                // 使用原始总冻结保证金按比例计算，避免累计误差
                frozenMargin = it->second.calculateReleaseAmount(openQty);
            }
        }
        
        // 冻结转占用
        accountManager_.confirmMargin(accountId, frozenMargin, margin);
        
        // 开仓
        positionManager_.openPosition(
            accountId, report.symbol, report.side,
            openQty, report.lastPx, margin);
        
        LOG() << "[SimulationApp] Open position: " << report.symbol
              << " side=" << static_cast<int>(report.side)
              << " qty=" << openQty
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
    // 通过 SessionManager 获取 Session 对象，提取真实的客户端标识
    auto session = sessionManager_.findSession(sessionID);
    if (session) {
        const std::string& clientId = session->get_client_comp_id();
        if (!clientId.empty()) {
            return clientId;
        }
    }
    // 安全策略：无法获取有效的 clientCompID 时返回空字符串
    // 调用方应检查返回值并拒绝处理
    LOG() << "[SimulationApp] Error: Could not extract clientCompID for session "
          << sessionID.to_string();
    return "";
}

Account SimulationApp::getOrCreateAccount(const std::string& accountId, double initialBalance) {
    auto accountOpt = accountManager_.getAccount(accountId);
    if (accountOpt) {
        return *accountOpt;
    }
    return accountManager_.createAccount(accountId, initialBalance);
}

// ============================================================================
// 消息处理函数实现
// ============================================================================

void SimulationApp::handleNewOrderSingle(const FixMessage& msg, const SessionID& sessionID, const std::string& userId) {
    // 解析订单
    ParseResult result = parseNewOrderSingle(msg, sessionID);
    if (!result.success) {
        LOG() << "[SimulationApp] Parse failed: " << result.error;
        ExecutionReport reject;
        reject.clOrdID = msg.has(tags::ClOrdID) ? msg.get_string(tags::ClOrdID) : "";
        reject.symbol = msg.has(tags::Symbol) ? msg.get_string(tags::Symbol) : "";
        reject.ordStatus = OrderStatus::REJECTED;
        reject.execTransType = ExecTransType::NEW;
        reject.ordRejReason = 99;
        reject.text = result.error;
        reject.transactTime = std::chrono::system_clock::now();
        onExecutionReport(sessionID, reject);
        return;
    }
    
    Order& order = result.order;
    
    // 关键：使用从 Session 提取的 userId，而非消息体中的 Account 字段
    // 这是安全路由的核心 - 防止用户伪造账户ID
    
    // 确保账户存在
    getOrCreateAccount(userId);
    
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
    auto accountOpt = accountManager_.getAccount(userId);
    if (!accountOpt) {
        LOG() << "[SimulationApp] Account not found: " << userId;
        return;
    }
    
    Position position;
    auto posOpt = positionManager_.getPosition(userId, order.symbol);
    if (posOpt) {
        position = *posOpt;
    } else {
        position.accountId = userId;
        position.instrumentId = order.symbol;
    }
    
    // 获取行情快照
    MarketDataSnapshot snapshot;
    const MarketDataSnapshot* snapshotPtr = engine_.getMarketSnapshot(order.symbol);
    if (snapshotPtr) {
        snapshot = *snapshotPtr;
    } else {
        snapshot.instrumentId = order.symbol;
        snapshot.upperLimitPrice = instrument->upperLimitPrice;
        snapshot.lowerLimitPrice = instrument->lowerLimitPrice;
    }
    
    // 判断开平标志
    OffsetFlag offsetFlag = OffsetFlag::OPEN;
    if (order.side == OrderSide::BUY && position.shortPosition > 0) {
        offsetFlag = OffsetFlag::CLOSE;
    } else if (order.side == OrderSide::SELL && position.longPosition > 0) {
        offsetFlag = OffsetFlag::CLOSE;
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
        if (!accountManager_.freezeMargin(userId, requiredMargin)) {
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
        
        {
            std::lock_guard<std::mutex> lock(mapMutex_);
            orderMarginInfoMap_[order.clOrdID] = OrderMarginInfo(requiredMargin, order.orderQty);
        }
    }
    
    // 记录订单到账户的映射
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        orderAccountMap_[order.clOrdID] = userId;
    }
    
    // 提交到撮合引擎（传入真实的用户ID）
    engine_.submit(OrderEvent::newOrder(order, userId));
}

void SimulationApp::handleOrderCancelRequest(const FixMessage& msg, const SessionID& sessionID, const std::string& userId) {
    CancelRequest req = parseCancelRequest(msg, sessionID);
    
    // 安全检查：验证撤单请求是否属于当前用户
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        auto it = orderAccountMap_.find(req.origClOrdID);
        if (it == orderAccountMap_.end()) {
            LOG() << "[SimulationApp] Cancel rejected: order " << req.origClOrdID << " not found";
            sendBusinessReject(sessionID, "F", "Order not found: " + req.origClOrdID);
            return;
        }
        if (it->second != userId) {
            LOG() << "[SimulationApp] Cancel rejected: order " << req.origClOrdID 
                  << " belongs to " << it->second << ", not " << userId;
            sendBusinessReject(sessionID, "F", "Not authorized to cancel this order");
            return;
        }
    }
    
    engine_.submit(OrderEvent::cancelRequest(req, userId));
}

void SimulationApp::handleBalanceQuery(const FixMessage& msg, const SessionID& sessionID, const std::string& userId) {
    LOG() << "[SimulationApp] Processing balance query for user: " << userId;
    
    // 查询账户数据
    auto accountOpt = accountManager_.getAccount(userId);
    if (!accountOpt) {
        LOG() << "[SimulationApp] Account not found for balance query: " << userId;
        sendBusinessReject(sessionID, "U1", "Account not found");
        return;
    }
    
    const Account& account = *accountOpt;
    
    // 构造 U2 响应消息 (BalanceQueryResponse)
    FixMessage response;
    response.set(tags::MsgType, "U2");
    
    // 回填请求ID（如果客户端发了的话）
    if (msg.has(tags::RequestID)) {
        response.set(tags::RequestID, msg.get_string(tags::RequestID));
    }
    
    // 账户标识
    response.set(tags::Account, userId);
    
    // 资金信息（使用自定义 Tag）
    // 注意：FixMessage::set 只支持 string 和 int，需要将 double 转为 string
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss.str(""); oss << account.balance;
    response.set(tags::Balance, oss.str());
    
    oss.str(""); oss << account.available;
    response.set(tags::Available, oss.str());
    
    oss.str(""); oss << account.frozenMargin;
    response.set(tags::FrozenMargin, oss.str());
    
    oss.str(""); oss << account.usedMargin;
    response.set(tags::UsedMargin, oss.str());
    
    oss.str(""); oss << account.positionProfit;
    response.set(tags::PositionProfit, oss.str());
    
    oss.str(""); oss << account.closeProfit;
    response.set(tags::CloseProfit, oss.str());
    
    oss.str(""); oss << account.getDynamicEquity();
    response.set(tags::DynamicEquity, oss.str());
    
    oss.str(""); oss << account.getRiskRatio();
    response.set(tags::RiskRatio, oss.str());
    
    // 发送响应
    if (!sessionManager_.sendMessage(sessionID, response)) {
        LOG() << "[SimulationApp] Failed to send balance response to " << sessionID.to_string();
    } else {
        LOG() << "[SimulationApp] Sent balance response to " << userId
              << " Balance=" << account.balance
              << " Available=" << account.available;
    }
}

void SimulationApp::handlePositionQuery(const FixMessage& msg, const SessionID& sessionID, const std::string& userId) {
    LOG() << "[SimulationApp] Processing position query for user: " << userId;
    
    // 获取用户的所有持仓
    auto positions = positionManager_.getPositionsByAccount(userId);
    
    // 构造 U4 响应消息 (PositionQueryResponse)
    FixMessage response;
    response.set(tags::MsgType, "U4");
    
    // 回填请求ID
    if (msg.has(tags::RequestID)) {
        response.set(tags::RequestID, msg.get_string(tags::RequestID));
    }
    
    response.set(tags::Account, userId);
    response.set(tags::NoPositions, static_cast<int>(positions.size()));
    
    // 注意：FIX 协议的 Repeating Group 处理比较复杂
    // 这里简化处理：将持仓信息序列化为文本格式放在 Text 字段
    // 实际生产环境应该使用标准的 Repeating Group 格式
    if (!positions.empty()) {
        std::ostringstream posText;
        posText << std::fixed << std::setprecision(2);
        for (const auto& pos : positions) {
            posText << pos.instrumentId << ":"
                    << "L" << pos.longPosition << "@" << pos.longAvgPrice << ","
                    << "S" << pos.shortPosition << "@" << pos.shortAvgPrice << ";";
        }
        response.set(tags::Text, posText.str());
    }
    
    // 发送响应
    if (!sessionManager_.sendMessage(sessionID, response)) {
        LOG() << "[SimulationApp] Failed to send position response to " << sessionID.to_string();
    } else {
        LOG() << "[SimulationApp] Sent position response to " << userId
              << " with " << positions.size() << " positions";
    }
}

void SimulationApp::sendBusinessReject(const SessionID& sessionID, const std::string& refMsgType, const std::string& reason) {
    // 构造 BusinessMessageReject (MsgType = j)
    FixMessage reject;
    reject.set(tags::MsgType, "j");
    reject.set(tags::Text, reason);
    // RefMsgType 标准 Tag 是 372，这里简化处理放在 Text 中
    
    LOG() << "[SimulationApp] Sending BusinessReject for MsgType=" << refMsgType
          << " reason: " << reason;
    
    if (!sessionManager_.sendMessage(sessionID, reject)) {
        LOG() << "[SimulationApp] Failed to send BusinessReject to " << sessionID.to_string();
    }
}

// ============================================================================
// 行情驱动账户更新实现
// ============================================================================

void SimulationApp::onMarketDataUpdate(const std::string& instrumentId, double lastPrice) {
    // 获取合约信息
    const Instrument* instrument = instrumentManager_.getInstrument(instrumentId);
    if (!instrument) {
        return;
    }
    
    // 获取该合约的所有持仓
    auto allPositions = positionManager_.getAllPositions();
    
    // 收集受影响的账户
    std::set<std::string> affectedAccounts;
    
    for (const auto& pos : allPositions) {
        if (pos.instrumentId == instrumentId && pos.hasPosition()) {
            // 更新持仓浮动盈亏
            double newProfit = positionManager_.updateProfit(
                pos.accountId, instrumentId, lastPrice, instrument->volumeMultiple);
            
            affectedAccounts.insert(pos.accountId);
            
            LOG() << "[SimulationApp] Updated position profit for " << pos.accountId
                  << " " << instrumentId << " profit=" << newProfit;
        }
    }
    
    // 更新受影响账户的持仓盈亏总额，并推送给 Client
    for (const auto& accountId : affectedAccounts) {
        double totalProfit = positionManager_.getTotalProfit(accountId);
        accountManager_.updatePositionProfit(accountId, totalProfit);
        
        // 推送账户更新给 Client（行情变化触发）
        pushAccountUpdate(accountId, 1);  // 1 = 行情变化
        
        // 同时推送持仓更新（包含最新盈亏）
        pushPositionUpdate(accountId, instrumentId, 1);  // 1 = 行情变化
    }
}

void SimulationApp::pushAccountUpdate(const std::string& userId, int reason) {
    // 查找用户对应的 Session
    auto sessionOpt = findSessionByUserId(userId);
    if (!sessionOpt) {
        LOG() << "[SimulationApp] Cannot push account update: session not found for " << userId;
        return;
    }
    
    // 获取账户数据
    auto accountOpt = accountManager_.getAccount(userId);
    if (!accountOpt) {
        return;
    }
    
    const Account& account = *accountOpt;
    
    // 构造 U5 推送消息 (AccountUpdateNotification)
    FixMessage msg;
    msg.set(tags::MsgType, "U5");
    msg.set(tags::Account, userId);
    msg.set(tags::UpdateType, 1);  // 1 = 账户更新
    msg.set(tags::UpdateReason, reason);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss.str(""); oss << account.balance;
    msg.set(tags::Balance, oss.str());
    
    oss.str(""); oss << account.available;
    msg.set(tags::Available, oss.str());
    
    oss.str(""); oss << account.frozenMargin;
    msg.set(tags::FrozenMargin, oss.str());
    
    oss.str(""); oss << account.usedMargin;
    msg.set(tags::UsedMargin, oss.str());
    
    oss.str(""); oss << account.positionProfit;
    msg.set(tags::PositionProfit, oss.str());
    
    oss.str(""); oss << account.getDynamicEquity();
    msg.set(tags::DynamicEquity, oss.str());
    
    // 发送推送
    if (!sessionManager_.sendMessage(*sessionOpt, msg)) {
        LOG() << "[SimulationApp] Failed to push account update to " << userId;
    }
}

void SimulationApp::pushPositionUpdate(const std::string& userId, const std::string& instrumentId, int reason) {
    // 查找用户对应的 Session
    auto sessionOpt = findSessionByUserId(userId);
    if (!sessionOpt) {
        return;
    }
    
    // 获取持仓数据
    auto posOpt = positionManager_.getPosition(userId, instrumentId);
    if (!posOpt) {
        return;
    }
    
    const Position& pos = *posOpt;
    
    // 构造 U6 推送消息 (PositionUpdateNotification)
    FixMessage msg;
    msg.set(tags::MsgType, "U6");
    msg.set(tags::Account, userId);
    msg.set(tags::UpdateType, 2);  // 2 = 持仓更新
    msg.set(tags::UpdateReason, reason);
    msg.set(tags::InstrumentID, instrumentId);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    msg.set(tags::LongPosition, static_cast<int>(pos.longPosition));
    
    oss.str(""); oss << pos.longAvgPrice;
    msg.set(tags::LongAvgPrice, oss.str());
    
    msg.set(tags::ShortPosition, static_cast<int>(pos.shortPosition));
    
    oss.str(""); oss << pos.shortAvgPrice;
    msg.set(tags::ShortAvgPrice, oss.str());
    
    oss.str(""); oss << pos.getTotalProfit();
    msg.set(tags::PositionProfit, oss.str());
    
    // 发送推送
    if (!sessionManager_.sendMessage(*sessionOpt, msg)) {
        LOG() << "[SimulationApp] Failed to push position update to " << userId;
    }
}

std::optional<SessionID> SimulationApp::findSessionByUserId(const std::string& userId) const {
    // 遍历所有 Session，找到 clientCompID == userId 的那个
    std::optional<SessionID> result;
    
    sessionManager_.forEachSession([&](const SessionID& sid, std::shared_ptr<Session> session) {
        // 已找到则跳过后续遍历
        if (result.has_value()) {
            return;
        }
        // 使用 Session::get_client_comp_id() 获取真实的客户端标识
        if (session && session->get_client_comp_id() == userId) {
            result = sid;
        }
    });
    
    return result;
}

// ============================================================================
// 合约搜索实现
// ============================================================================

void SimulationApp::handleInstrumentSearch(const FixMessage& msg, const SessionID& sessionID) {
    // 获取搜索参数
    std::string pattern;
    if (msg.has(tags::SearchPattern)) {
        pattern = msg.get_string(tags::SearchPattern);
    }
    
    size_t maxResults = 10;  // 默认返回 10 条
    if (msg.has(tags::MaxResults)) {
        maxResults = static_cast<size_t>(msg.get_int(tags::MaxResults));
        if (maxResults > 50) maxResults = 50;  // 限制最大返回数量
    }
    
    LOG() << "[SimulationApp] Processing instrument search: pattern=" << pattern
          << " maxResults=" << maxResults;
    
    // 搜索合约
    auto results = instrumentManager_.searchByPrefix(pattern, maxResults);
    
    // 构造 U8 响应消息 (InstrumentSearchResponse)
    FixMessage response;
    response.set(tags::MsgType, "U8");
    
    // 回填请求ID
    if (msg.has(tags::RequestID)) {
        response.set(tags::RequestID, msg.get_string(tags::RequestID));
    }
    
    response.set(tags::SearchPattern, pattern);
    response.set(tags::ResultCount, static_cast<int>(results.size()));
    
    // 将合约列表序列化为逗号分隔的字符串
    if (!results.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) oss << ",";
            oss << results[i];
        }
        response.set(tags::InstrumentList, oss.str());
    }
    
    // 发送响应
    if (!sessionManager_.sendMessage(sessionID, response)) {
        LOG() << "[SimulationApp] Failed to send instrument search response to "
              << sessionID.to_string();
    } else {
        LOG() << "[SimulationApp] Sent instrument search response: " << results.size() << " results";
    }
}

} // namespace fix40
