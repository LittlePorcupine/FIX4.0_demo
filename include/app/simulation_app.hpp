/**
 * @file simulation_app.hpp
 * @brief 模拟交易应用层实现
 *
 * 提供一个线程安全的 Application 实现，演示最佳实践：
 * - fromApp() 只做轻量的入队操作
 * - 业务逻辑由独立的撮合引擎线程处理
 */

#pragma once

#include "fix/application.hpp"
#include "fix/session_manager.hpp"
#include "app/matching_engine.hpp"

namespace fix40 {

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
 * @par 支持的消息类型
 * - D: NewOrderSingle (新订单)
 * - F: OrderCancelRequest (撤单请求)
 * - G: OrderCancelReplaceRequest (改单请求)
 *
 * @par 使用示例
 * @code
 * SimulationApp app;
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
     * @brief 构造模拟交易应用
     */
    SimulationApp();

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

private:
    /**
     * @brief ExecutionReport 回调处理
     * @param sessionID 目标会话
     * @param report 执行报告
     *
     * 将 ExecutionReport 转换为 FIX 消息并发送到客户端。
     */
    void onExecutionReport(const SessionID& sessionID, const ExecutionReport& report);

    MatchingEngine engine_;           ///< 撮合引擎
    SessionManager sessionManager_;   ///< 会话管理器
};

} // namespace fix40
