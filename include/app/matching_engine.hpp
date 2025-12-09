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
#include "base/blockingconcurrentqueue.h"
#include "app/order_event.hpp"

namespace fix40 {

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
 * engine.start();
 * 
 * // 从 Application::fromApp() 中提交事件
 * engine.submit(OrderEvent{OrderEventType::NEW_ORDER, sessionID, msg});
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

    std::atomic<bool> running_{false};  ///< 运行状态
    std::thread worker_thread_;          ///< 工作线程
    
    /// 订单事件队列（无锁阻塞队列）
    moodycamel::BlockingConcurrentQueue<OrderEvent> event_queue_;
};

} // namespace fix40
