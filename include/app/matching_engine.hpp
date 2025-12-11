/**
 * @file matching_engine.hpp
 * @brief 撮合引擎接口
 *
 * 提供独立线程运行的撮合引擎，从无锁队列消费订单事件并处理。
 */

#pragma once

#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <memory>
#include "base/blockingconcurrentqueue.h"
#include "app/order_event.hpp"
#include "app/order_book.hpp"

namespace fix40 {

/**
 * @brief ExecutionReport 回调类型
 * 
 * 当撮合引擎产生执行报告时调用此回调。
 * 参数：sessionID - 目标会话，report - 执行报告
 */
using ExecutionReportCallback = std::function<void(const SessionID&, const ExecutionReport&)>;

/**
 * @class MatchingEngine
 * @brief 撮合引擎
 *
 * 在独立线程中运行，从无锁队列消费订单事件并处理。
 * 这种设计确保：
 * - Application::fromApp() 只做轻量的入队操作，快速返回
 * - 所有订单处理在单线程中串行执行，无需加锁
 * - 工作线程不会因业务逻辑阻塞
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
     * @brief 获取订单簿（只读）
     * @param symbol 合约代码
     * @return const OrderBook* 订单簿指针，不存在返回 nullptr
     */
    const OrderBook* getOrderBook(const std::string& symbol) const;

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
     * @brief 处理新订单
     * @param event 订单事件
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

    std::atomic<bool> running_{false};  ///< 运行状态
    std::thread worker_thread_;          ///< 工作线程
    
    /// 订单事件队列（无锁阻塞队列）
    moodycamel::BlockingConcurrentQueue<OrderEvent> event_queue_;

    /// 订单簿映射：symbol -> OrderBook
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> orderBooks_;

    /// 订单到会话的映射：clOrdID -> SessionID（用于成交通知）
    std::unordered_map<std::string, SessionID> orderSessionMap_;

    /// ExecutionReport 回调
    ExecutionReportCallback execReportCallback_;

    /// ExecID 计数器
    uint64_t nextExecID_ = 1;
};

} // namespace fix40
