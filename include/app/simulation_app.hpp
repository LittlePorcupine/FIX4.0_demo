/**
 * @file simulation_app.hpp
 * @brief 模拟交易网关 (Trade Gateway)
 *
 * 提供一个线程安全的 Application 实现，作为交易网关：
 * - 身份绑定：利用 FIX Logon 消息中的 SenderCompID 作为用户标识
 * - 安全路由：强制使用 Session 绑定的 UserID 进行验资和下单
 * - 协议扩展：支持 FIX User Defined Message (U系列) 实现自定义查询
 * 
 * 集成以下模块：
 * - AccountManager: 账户管理
 * - PositionManager: 持仓管理
 * - InstrumentManager: 合约信息管理
 * - RiskManager: 风险控制
 * 
 * @par 支持的消息类型
 * - D:  NewOrderSingle (新订单)
 * - F:  OrderCancelRequest (撤单请求)
 * - U1: BalanceQueryRequest (资金查询请求) - 自定义
 * - U3: PositionQueryRequest (持仓查询请求) - 自定义
 * 
 * @par 发送的消息类型
 * - 8:  ExecutionReport (执行报告)
 * - U2: BalanceQueryResponse (资金查询响应) - 自定义
 * - U4: PositionQueryResponse (持仓查询响应) - 自定义
 */

#pragma once

#include "fix/application.hpp"
#include "fix/session_manager.hpp"
#include "app/engine/matching_engine.hpp"
#include "app/manager/account_manager.hpp"
#include "app/manager/position_manager.hpp"
#include "app/manager/instrument_manager.hpp"
#include "app/manager/risk_manager.hpp"
#include <memory>
#include <unordered_map>
#include <mutex>

namespace fix40 {

// 前向声明
class IStore;
struct SimulationAppTestAccess;

/**
 * @class SimulationApp
 * @brief 模拟交易应用层
 *
 * 实现 Application 接口，采用生产者-消费者模式处理业务消息：
 * - fromApp() 将消息封装为事件，push 到无锁队列（生产者）
 * - MatchingEngine 在独立线程中消费并处理事件（消费者）
 *
 * 这种设计确保：
 * - Application 回调快速返回，不阻塞工作线程
 * - 所有订单处理在单线程中串行执行，无需加锁
 * - 异常隔离：撮合引擎的异常不会影响网络层
 *
 * @par 集成模块
 * - AccountManager: 账户管理（资金冻结/释放）
 * - PositionManager: 持仓管理（开仓/平仓）
 * - InstrumentManager: 合约信息管理
 * - RiskManager: 风险控制（资金/价格/持仓检查）
 *
 * @par 支持的消息类型
 * - D: NewOrderSingle (新订单)
 * - F: OrderCancelRequest (撤单请求)
 * - G: OrderCancelReplaceRequest (改单请求)
 *
 * @par 使用示例
 * @code
 * IStore* store = ...;  // 存储接口
 * SimulationApp app(store);
 * 
 * // 加载合约配置
 * app.getInstrumentManager().loadFromConfig("config/instruments.json");
 * 
 * app.start();  // 启动撮合引擎
 * 
 * session->set_application(&app);
 * 
 * // 关闭时
 * app.stop();
 * @endcode
 */
class SimulationApp : public Application {
public:
    /**
     * @struct OrderMarginInfo
     * @brief 订单保证金信息
     * 
     * 用于正确处理部分成交时的保证金计算。
     * 存储原始总冻结保证金和订单总数量，避免累计误差。
     */
    struct OrderMarginInfo {
        double originalFrozenMargin;  ///< 原始总冻结保证金
        int64_t originalOrderQty;     ///< 原始订单总数量
        double releasedMargin;        ///< 已释放的保证金（累计）
        
        OrderMarginInfo() 
            : originalFrozenMargin(0.0)
            , originalOrderQty(0)
            , releasedMargin(0.0) {}
        
        OrderMarginInfo(double frozen, int64_t qty)
            : originalFrozenMargin(frozen)
            , originalOrderQty(qty)
            , releasedMargin(0.0) {}
        
        /// 计算本次成交应释放的冻结保证金
        double calculateReleaseAmount(int64_t fillQty) {
            if (originalOrderQty <= 0) return 0.0;
            double amount = originalFrozenMargin * fillQty / originalOrderQty;
            releasedMargin += amount;
            return amount;
        }
        
        /// 获取剩余未释放的冻结保证金
        double getRemainingFrozen() const {
            return originalFrozenMargin - releasedMargin;
        }
    };

    /**
     * @brief 默认构造函数（不带持久化）
     */
    SimulationApp();

    /**
     * @brief 带存储接口的构造函数
     * 
     * @param store 存储接口指针，用于账户和持仓的持久化
     */
    explicit SimulationApp(IStore* store);

    /**
     * @brief 析构函数
     *
     * 自动停止撮合引擎。
     */
    ~SimulationApp() override;

    /**
     * @brief 启动撮合引擎
     *
     * 必须在处理消息前调用。
     */
    void start();

    /**
     * @brief 停止撮合引擎
     *
     * 等待当前处理完成后退出。
     */
    void stop();

    // =========================================================================
    // Application 接口实现
    // =========================================================================

    /**
     * @brief 会话登录成功回调
     * @param sessionID 已建立的会话标识符
     *
     * 将登录事件提交到撮合引擎队列。
     */
    void onLogon(const SessionID& sessionID) override;

    /**
     * @brief 会话登出回调
     * @param sessionID 即将断开的会话标识符
     *
     * 将登出事件提交到撮合引擎队列。
     */
    void onLogout(const SessionID& sessionID) override;

    /**
     * @brief 收到业务消息回调
     * @param msg 收到的 FIX 业务消息
     * @param sessionID 消息来源的会话标识符
     *
     * 将消息封装为事件，提交到撮合引擎队列。
     * 此方法只做轻量的入队操作，快速返回。
     */
    void fromApp(const FixMessage& msg, const SessionID& sessionID) override;

    /**
     * @brief 发送业务消息前回调
     * @param msg 即将发送的 FIX 业务消息
     * @param sessionID 发送消息的会话标识符
     *
     * 用于记录审计日志。
     */
    void toApp(FixMessage& msg, const SessionID& sessionID) override;

    /**
     * @brief 获取会话管理器
     * @return SessionManager& 会话管理器引用
     *
     * 返回内部会话管理器的引用，调用者可通过该引用调用
     * registerSession()/unregisterSession() 等方法管理会话。
     */
    SessionManager& getSessionManager() { return sessionManager_; }

    /**
     * @brief 获取存储接口
     * @return IStore* 存储接口指针，可能为 nullptr
     *
     * 该指针用于账户/持仓等数据的持久化与重启恢复。
     */
    IStore* getStore() const override { return store_; }

    // =========================================================================
    // 管理器访问接口
    // =========================================================================

    /**
     * @brief 获取账户管理器
     * @return AccountManager& 账户管理器引用
     */
    AccountManager& getAccountManager() { return accountManager_; }

    /**
     * @brief 获取持仓管理器
     * @return PositionManager& 持仓管理器引用
     */
    PositionManager& getPositionManager() { return positionManager_; }

    /**
     * @brief 获取合约管理器
     * @return InstrumentManager& 合约管理器引用
     */
    InstrumentManager& getInstrumentManager() { return instrumentManager_; }

    /**
     * @brief 获取风控管理器
     * @return RiskManager& 风控管理器引用
     */
    RiskManager& getRiskManager() { return riskManager_; }

    /**
     * @brief 获取撮合引擎
     * @return MatchingEngine& 撮合引擎引用
     */
    MatchingEngine& getMatchingEngine() { return engine_; }

    // =========================================================================
    // 账户操作接口
    // =========================================================================

    /**
     * @brief 创建或获取账户
     * 
     * 如果账户不存在，创建一个新账户；否则返回现有账户。
     * 
     * @param accountId 账户ID
     * @param initialBalance 初始余额（仅在创建时使用）
     * @return Account 账户信息
     */
    Account getOrCreateAccount(const std::string& accountId, double initialBalance = 1000000.0);

private:
    /**
     * @brief 单元测试访问器
     *
     * 仅用于单元测试访问内部实现细节（例如 pushAccountUpdate），避免在测试中使用
     * `#define private public` 破坏标准库头文件的可见性/封装并导致编译失败。
     */
    friend struct SimulationAppTestAccess;

    /**
     * @brief 初始化各管理器
     * 
     * 设置撮合引擎与各管理器的关联。
     */
    void initializeManagers();

    /**
     * @brief ExecutionReport 回调处理
     * @param sessionID 目标会话
     * @param report 执行报告
     *
     * 将 ExecutionReport 转换为 FIX 消息并发送到客户端。
     * 同时更新账户和持仓状态。
     */
    void onExecutionReport(const SessionID& sessionID, const ExecutionReport& report);

    /**
     * @brief 处理订单成交
     * 
     * 更新账户资金和持仓状态。
     * 
     * @param accountId 账户ID
     * @param report 执行报告
     */
    void handleFill(const std::string& accountId, const ExecutionReport& report);

    /**
     * @brief 处理订单拒绝
     * 
     * 释放冻结的保证金。
     * 
     * @param accountId 账户ID
     * @param report 执行报告
     */
    void handleReject(const std::string& accountId, const ExecutionReport& report);

    /**
     * @brief 处理订单撤销
     * 
     * 释放冻结的保证金。
     * 
     * @param accountId 账户ID
     * @param report 执行报告
     */
    void handleCancel(const std::string& accountId, const ExecutionReport& report);

    /**
     * @brief 从SessionID提取账户ID
     * 
     * 使用 SenderCompID 作为账户ID，实现身份绑定。
     * 
     * @param sessionID 会话ID
     * @return 账户ID（使用SenderCompID）
     */
    std::string extractAccountId(const SessionID& sessionID) const;

    // =========================================================================
    // 消息处理函数 (Message Handlers)
    // =========================================================================

    /**
     * @brief 处理新订单 (MsgType = D)
     * 
     * @param msg FIX 消息
     * @param sessionID 会话标识
     * @param userId 绑定的用户ID（从 Session 提取，非消息体）
     */
    void handleNewOrderSingle(const FixMessage& msg, const SessionID& sessionID, const std::string& userId);

    /**
     * @brief 处理撤单请求 (MsgType = F)
     * 
     * @param msg FIX 消息
     * @param sessionID 会话标识
     * @param userId 绑定的用户ID
     */
    void handleOrderCancelRequest(const FixMessage& msg, const SessionID& sessionID, const std::string& userId);

    /**
     * @brief 处理资金查询请求 (MsgType = U1)
     * 
     * 查询用户账户的资金信息，返回 U2 响应。
     * 
     * @param msg FIX 消息
     * @param sessionID 会话标识
     * @param userId 绑定的用户ID
     */
    void handleBalanceQuery(const FixMessage& msg, const SessionID& sessionID, const std::string& userId);

    /**
     * @brief 处理持仓查询请求 (MsgType = U3)
     * 
     * 查询用户的持仓信息，返回 U4 响应。
     * 
     * @param msg FIX 消息
     * @param sessionID 会话标识
     * @param userId 绑定的用户ID
     */
    void handlePositionQuery(const FixMessage& msg, const SessionID& sessionID, const std::string& userId);

    /**
     * @brief 处理合约搜索请求 (MsgType = U7)
     * 
     * 根据前缀搜索合约，返回 U8 响应。
     * 用于 Client 端的合约代码自动补全功能。
     * 
     * @param msg FIX 消息
     * @param sessionID 会话标识
     */
    void handleInstrumentSearch(const FixMessage& msg, const SessionID& sessionID);

    /**
     * @brief 处理订单历史查询请求 (MsgType = U9)
     *
     * 从服务端持久化存储中加载该用户的历史订单，并返回 U10 响应。
     *
     * @param msg FIX 请求消息
     * @param sessionID 会话标识
     * @param userId 绑定的用户ID（从 Session 提取，非消息体）
     *
     * @note 由于 FIX RepeatingGroup 实现较复杂，本项目将订单列表序列化为文本放入 Text(58)。
     */
    void handleOrderHistoryQuery(const FixMessage& msg, const SessionID& sessionID, const std::string& userId);

    /**
     * @brief 发送拒绝消息
     * 
     * 当收到无法处理的消息时，发送 BusinessMessageReject。
     * 
     * @param sessionID 会话标识
     * @param refMsgType 被拒绝的消息类型
     * @param reason 拒绝原因
     */
    void sendBusinessReject(const SessionID& sessionID, const std::string& refMsgType, const std::string& reason);

    // =========================================================================
    // 行情驱动账户更新 (Market-Driven Account Update)
    // =========================================================================

    /**
     * @brief 处理行情更新，重算所有相关账户的价值
     * 
     * 当行情变化时调用，更新持仓浮动盈亏和账户价值。
     * 可选择性地推送更新给相关 Client。
     * 
     * @param instrumentId 合约代码
     * @param lastPrice 最新价
     */
    void onMarketDataUpdate(const std::string& instrumentId, double lastPrice);

    /**
     * @brief 向指定用户推送账户更新 (MsgType = U5)
     * 
     * 当账户价值发生变化时，主动推送给 Client。
     * 
     * @param userId 用户ID
     * @param reason 更新原因 (1=行情变化, 2=成交, 3=出入金)
     */
    void pushAccountUpdate(const std::string& userId, int reason = 1);

    /**
     * @brief 向指定用户推送持仓更新 (MsgType = U6)
     * 
     * 当持仓发生变化时，主动推送给 Client。
     * 
     * @param userId 用户ID
     * @param instrumentId 合约代码
     * @param reason 更新原因
     */
    void pushPositionUpdate(const std::string& userId, const std::string& instrumentId, int reason = 1);

    /**
     * @brief 根据用户ID查找对应的 SessionID
     * 
     * @param userId 用户ID
     * @return std::optional<SessionID> 找到返回 SessionID，否则返回 nullopt
     */
    std::optional<SessionID> findSessionByUserId(const std::string& userId) const;

    // =========================================================================
    // 成员变量
    // =========================================================================

    MatchingEngine engine_;              ///< 撮合引擎
    SessionManager sessionManager_;      ///< 会话管理器
    
    AccountManager accountManager_;      ///< 账户管理器
    PositionManager positionManager_;    ///< 持仓管理器
    InstrumentManager instrumentManager_; ///< 合约管理器
    RiskManager riskManager_;            ///< 风控管理器
    
    IStore* store_ = nullptr;            ///< 存储接口（可为nullptr）

    /// 订单到账户的映射：clOrdID -> accountId
    std::unordered_map<std::string, std::string> orderAccountMap_;
    
    /// 订单保证金信息映射：clOrdID -> OrderMarginInfo
    std::unordered_map<std::string, OrderMarginInfo> orderMarginInfoMap_;
    
    /// 互斥锁，保护映射表
    mutable std::mutex mapMutex_;
};

} // namespace fix40
