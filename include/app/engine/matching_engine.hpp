/**
 * @file matching_engine.hpp
 * @brief 撮合引擎接口
 *
 * 提供独立线程运行的撮合引擎，从无锁队列消费订单事件并处理。
 * 支持行情驱动撮合模式：用户订单与CTP行情盘口比对撮合。
 */

#pragma once

#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <list>
#include <memory>
#include "base/blockingconcurrentqueue.h"
#include "app/engine/order_event.hpp"
#include "app/engine/order_book.hpp"
#include "app/model/market_data_snapshot.hpp"
#include "market/market_data.hpp"

namespace fix40 {

// 前向声明
class RiskManager;
class AccountManager;
class PositionManager;
class InstrumentManager;

/**
 * @brief ExecutionReport 回调类型
 * 
 * 当撮合引擎产生执行报告时调用此回调。
 * 参数：sessionID - 目标会话，report - 执行报告
 */
using ExecutionReportCallback = std::function<void(const SessionID&, const ExecutionReport&)>;

/**
 * @brief 行情更新回调类型
 * 
 * 当行情数据更新时调用此回调，用于触发账户价值重算。
 * 参数：instrumentId - 合约代码，lastPrice - 最新价
 */
using MarketDataUpdateCallback = std::function<void(const std::string&, double)>;

/**
 * @class MatchingEngine
 * @brief 行情驱动撮合引擎
 *
 * 在独立线程中运行，从无锁队列消费订单事件并处理。
 * 
 * @par 与原有撮合引擎的区别
 * - 原有：用户订单之间互相撮合
 * - 新版：用户订单与CTP行情盘口比对撮合
 *
 * @par 设计特点
 * - Application::fromApp() 只做轻量的入队操作，快速返回
 * - 所有订单处理在单线程中串行执行，无需加锁
 * - 工作线程不会因业务逻辑阻塞
 * - 支持行情驱动的被动撮合
 *
 * @par 撮合规则
 * - 买单成交条件：买价 >= CTP卖一价
 * - 卖单成交条件：卖价 <= CTP买一价
 * - 成交价格：取CTP对手盘价格
 *
 * @par 使用示例
 * @code
 * MatchingEngine engine;
 * engine.setExecutionReportCallback([](const SessionID& sid, const ExecutionReport& rpt) {
 *     // 发送 ExecutionReport 到客户端
 * });
 * engine.start();
 * 
 * // 从 Application::fromApp() 中提交事件
 * engine.submit(OrderEvent{OrderEventType::NEW_ORDER, sessionID, order});
 * 
 * // 提交行情数据触发撮合
 * engine.submitMarketData(marketData);
 * 
 * // 关闭时
 * engine.stop();
 * @endcode
 */
class MatchingEngine {
public:
    /**
     * @brief 构造撮合引擎
     */
    MatchingEngine();

    /**
     * @brief 析构函数
     *
     * 自动停止引擎线程。
     */
    ~MatchingEngine();

    // 禁止拷贝
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    /**
     * @brief 启动撮合引擎线程
     */
    void start();

    /**
     * @brief 停止撮合引擎线程
     *
     * 等待当前处理完成后退出。
     */
    void stop();

    /**
     * @brief 提交订单事件
     * @param event 订单事件
     *
     * 线程安全，可从任意线程调用。
     * 事件会被放入无锁队列，由引擎线程异步处理。
     */
    void submit(const OrderEvent& event);

    /**
     * @brief 提交订单事件（移动语义）
     * @param event 订单事件
     */
    void submit(OrderEvent&& event);

    /**
     * @brief 检查引擎是否正在运行
     * @return true 正在运行
     * @return false 已停止
     */
    bool is_running() const { return running_.load(); }

    /**
     * @brief 设置 ExecutionReport 回调
     * @param callback 回调函数
     * 
     * 必须在 start() 之前调用。
     */
    void setExecutionReportCallback(ExecutionReportCallback callback) {
        execReportCallback_ = std::move(callback);
    }

    /**
     * @brief 设置行情更新回调
     * @param callback 回调函数
     * 
     * 当行情数据更新时调用，用于触发账户价值重算和推送。
     * 必须在 start() 之前调用。
     */
    void setMarketDataUpdateCallback(MarketDataUpdateCallback callback) {
        marketDataUpdateCallback_ = std::move(callback);
    }

    /**
     * @brief 获取订单簿（只读）
     * @param symbol 合约代码
     * @return const OrderBook* 订单簿指针，不存在返回 nullptr
     */
    const OrderBook* getOrderBook(const std::string& symbol) const;

    // =========================================================================
    // 行情驱动撮合接口
    // =========================================================================

    /**
     * @brief 提交行情数据
     *
     * 当新行情到达时，检查虚拟订单簿中是否有可成交的挂单。
     * 线程安全，可从任意线程调用。
     *
     * @param md 行情数据
     */
    void submitMarketData(const MarketData& md);

    /**
     * @brief 获取行情快照
     *
     * @param instrumentId 合约代码
     * @return const MarketDataSnapshot* 行情快照指针，不存在返回 nullptr
     */
    const MarketDataSnapshot* getMarketSnapshot(const std::string& instrumentId) const;

    /**
     * @brief 获取挂单列表（只读）
     *
     * @param instrumentId 合约代码
     * @return const std::list<Order>* 挂单列表指针，不存在返回 nullptr
     */
    const std::list<Order>* getPendingOrders(const std::string& instrumentId) const;

    /**
     * @brief 获取所有挂单数量
     *
     * @return 所有合约的挂单总数
     */
    size_t getTotalPendingOrderCount() const;

    // =========================================================================
    // 管理器设置（用于集成风控和资金/持仓管理）
    // =========================================================================

    /**
     * @brief 设置风控管理器
     * @param riskMgr 风控管理器指针
     */
    void setRiskManager(RiskManager* riskMgr) { riskManager_ = riskMgr; }

    /**
     * @brief 设置账户管理器
     * @param accountMgr 账户管理器指针
     */
    void setAccountManager(AccountManager* accountMgr) { accountManager_ = accountMgr; }

    /**
     * @brief 设置持仓管理器
     * @param positionMgr 持仓管理器指针
     */
    void setPositionManager(PositionManager* positionMgr) { positionManager_ = positionMgr; }

    /**
     * @brief 设置合约管理器
     * @param instrumentMgr 合约管理器指针
     */
    void setInstrumentManager(InstrumentManager* instrumentMgr) { instrumentManager_ = instrumentMgr; }

    // =========================================================================
    // 行情驱动撮合核心方法（公开以便测试）
    // =========================================================================

    /**
     * @brief 检查买单是否可成交
     *
     * 买单成交条件：买价 >= CTP卖一价
     *
     * @param order 待检查的买单
     * @param snapshot 当前行情快照
     * @return 可成交返回 true
     */
    bool canMatchBuyOrder(const Order& order, const MarketDataSnapshot& snapshot) const;

    /**
     * @brief 检查卖单是否可成交
     *
     * 卖单成交条件：卖价 <= CTP买一价
     *
     * @param order 待检查的卖单
     * @param snapshot 当前行情快照
     * @return 可成交返回 true
     */
    bool canMatchSellOrder(const Order& order, const MarketDataSnapshot& snapshot) const;

private:
    /**
     * @brief 引擎主循环
     */
    void run();

    /**
     * @brief 处理单个订单事件
     * @param event 订单事件
     */
    void process_event(const OrderEvent& event);

    /**
     * @brief 处理新订单（行情驱动模式）
     * @param event 订单事件
     *
     * 流程：
     * 1. 风控检查（如果设置了RiskManager）
     * 2. 尝试立即撮合（与当前行情比对）
     * 3. 未成交部分挂入虚拟订单簿等待行情触发
     */
    void handle_new_order(const OrderEvent& event);

    /**
     * @brief 处理撤单请求
     * @param event 订单事件
     */
    void handle_cancel_request(const OrderEvent& event);

    /**
     * @brief 处理会话登录
     * @param event 事件
     */
    void handle_session_logon(const OrderEvent& event);

    /**
     * @brief 处理会话登出
     * @param event 事件
     */
    void handle_session_logout(const OrderEvent& event);

    /**
     * @brief 获取或创建订单簿
     * @param symbol 合约代码
     * @return OrderBook& 订单簿引用
     */
    OrderBook& getOrCreateOrderBook(const std::string& symbol);

    /**
     * @brief 发送 ExecutionReport
     * @param sessionID 目标会话
     * @param report 执行报告
     */
    void sendExecutionReport(const SessionID& sessionID, const ExecutionReport& report);

    /**
     * @brief 生成 ExecID
     */
    std::string generateExecID();

    /**
     * @brief 生成 OrderID
     */
    std::string generateOrderID();

    // =========================================================================
    // 行情驱动撮合内部方法
    // =========================================================================

    /**
     * @brief 处理行情更新
     *
     * 1. 更新行情快照
     * 2. 遍历该合约的挂单
     * 3. 检查是否满足成交条件
     * 4. 触发成交
     *
     * @param md 行情数据
     */
    void handleMarketData(const MarketData& md);

    /**
     * @brief 尝试撮合订单（行情驱动）
     *
     * @param order 待撮合订单
     * @param snapshot 当前行情快照
     * @return 是否成交
     */
    bool tryMatch(Order& order, const MarketDataSnapshot& snapshot);

    /**
     * @brief 执行成交
     *
     * @param order 成交的订单
     * @param fillPrice 成交价格
     * @param fillQty 成交数量
     */
    void executeFill(Order& order, double fillPrice, int64_t fillQty);

    /**
     * @brief 将订单添加到挂单列表
     *
     * @param order 待挂单的订单
     */
    void addToPendingOrders(const Order& order);

    /**
     * @brief 从挂单列表移除订单
     *
     * @param instrumentId 合约代码
     * @param clOrdID 客户订单ID
     * @return 被移除的订单，不存在返回 nullopt
     */
    std::optional<Order> removeFromPendingOrders(const std::string& instrumentId, 
                                                   const std::string& clOrdID);

    std::atomic<bool> running_{false};  ///< 运行状态
    std::thread worker_thread_;          ///< 工作线程
    
    /// 订单事件队列（无锁阻塞队列）
    moodycamel::BlockingConcurrentQueue<OrderEvent> event_queue_;

    /// 订单簿映射：symbol -> OrderBook（保留用于兼容）
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> orderBooks_;

    /// 订单到会话的映射：clOrdID -> SessionID（用于成交通知）
    std::unordered_map<std::string, SessionID> orderSessionMap_;

    /// 订单到用户ID的映射：clOrdID -> userId（用于持仓更新）
    std::unordered_map<std::string, std::string> orderUserMap_;

    /// ExecutionReport 回调
    ExecutionReportCallback execReportCallback_;

    /// 行情更新回调
    MarketDataUpdateCallback marketDataUpdateCallback_;

    /// ExecID 计数器
    uint64_t nextExecID_ = 1;

    /// OrderID 计数器
    uint64_t nextOrderID_ = 1;

    // =========================================================================
    // 行情驱动撮合相关成员
    // =========================================================================

    /// 行情快照：instrumentId -> snapshot
    std::unordered_map<std::string, MarketDataSnapshot> marketSnapshots_;

    /// 虚拟订单簿（挂单列表）：instrumentId -> 挂单列表
    std::unordered_map<std::string, std::list<Order>> pendingOrders_;

    /// 行情数据队列（无锁阻塞队列）
    moodycamel::BlockingConcurrentQueue<MarketData> marketDataQueue_;

    // =========================================================================
    // 管理器指针（用于集成风控和资金/持仓管理）
    // =========================================================================

    RiskManager* riskManager_ = nullptr;           ///< 风控管理器
    AccountManager* accountManager_ = nullptr;     ///< 账户管理器
    PositionManager* positionManager_ = nullptr;   ///< 持仓管理器
    InstrumentManager* instrumentManager_ = nullptr; ///< 合约管理器
};

} // namespace fix40
