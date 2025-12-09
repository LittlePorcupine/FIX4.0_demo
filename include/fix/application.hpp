/**
 * @file application.hpp
 * @brief FIX 应用层接口定义
 *
 * 定义业务逻辑与会话层的分离接口，允许用户实现自定义的
 * 业务消息处理逻辑，而无需修改底层会话管理代码。
 */

#pragma once

#include <string>
#include <functional>
#include "fix/fix_codec.hpp"

namespace fix40 {

// 前置声明
class Session;

/**
 * @struct SessionID
 * @brief FIX 会话标识符
 *
 * 唯一标识一个 FIX 会话，由发送方和接收方的 CompID 组成。
 * 用于在多会话环境中区分不同的连接。
 */
struct SessionID {
    std::string senderCompID;  ///< 发送方标识符
    std::string targetCompID;  ///< 接收方标识符

    /**
     * @brief 默认构造函数
     */
    SessionID() = default;

    /**
     * @brief 构造会话标识符
     * @param sender 发送方 CompID
     * @param target 接收方 CompID
     */
    SessionID(const std::string& sender, const std::string& target)
        : senderCompID(sender), targetCompID(target) {}

    /**
     * @brief 转换为字符串表示
     * @return std::string 格式为 "sender->target"
     */
    std::string to_string() const {
        return senderCompID + "->" + targetCompID;
    }

    /**
     * @brief 相等比较
     */
    bool operator==(const SessionID& other) const {
        return senderCompID == other.senderCompID && 
               targetCompID == other.targetCompID;
    }

    /**
     * @brief 不等比较
     */
    bool operator!=(const SessionID& other) const {
        return !(*this == other);
    }
};

/**
 * @class Application
 * @brief FIX 应用层抽象接口
 *
 * 该接口将业务逻辑与 FIX 会话层分离。Session 负责处理会话层消息
 * （Logon、Logout、Heartbeat、TestRequest），而业务消息（如订单、
 * 执行报告等）则委托给 Application 实现类处理。
 *
 * @par 回调分类
 * - 业务消息回调：fromApp / toApp
 * - 管理消息回调：fromAdmin / toAdmin
 * - 生命周期回调：onLogon / onLogout
 *
 * @par 线程安全
 * @warning Application 的回调方法可能被多个工作线程并发调用！
 * 
 * 由于 FixServer 使用线程池，不同客户端连接绑定到不同工作线程，
 * 当多个客户端同时发送消息时，fromApp() 会被并发调用。
 * 
 * 推荐的线程安全实现方式：
 * 1. fromApp() 只做轻量操作：将消息封装后 push 到无锁队列
 * 2. 由独立的业务处理线程（如撮合引擎）从队列消费并处理
 * 3. 避免在 fromApp() 中执行耗时操作或持有锁
 *
 * @par 异常处理
 * Session 会捕获 Application 回调中抛出的异常，记录日志后继续运行。
 * 但强烈建议在 Application 实现中自行处理异常，避免依赖外部捕获。
 *
 * @par 生命周期
 * Application 实例的生命周期应由 Server/Client 管理，
 * 且必须比关联的 Session 更长。Session 仅持有裸指针。
 *
 * @par 使用示例
 * @code
 * class MyTradingApp : public Application {
 * public:
 *     void onLogon(const SessionID& sessionID) override {
 *         LOG() << "Session logged on: " << sessionID.to_string();
 *     }
 *     
 *     void fromApp(const FixMessage& msg, const SessionID& sessionID) override {
 *         // 推荐：只做轻量操作，push 到队列
 *         order_queue_.enqueue({msg, sessionID});
 *     }
 * };
 * @endcode
 */
class Application {
public:
    /**
     * @brief 虚析构函数
     */
    virtual ~Application() = default;

    // =========================================================================
    // 生命周期回调
    // =========================================================================

    /**
     * @brief 会话登录成功回调
     * @param sessionID 已建立的会话标识符
     *
     * 当 FIX 会话成功建立（收到 Logon 确认）后调用。
     * 可用于初始化交易状态、订阅行情等。
     *
     * @note 可能被多个工作线程并发调用
     */
    virtual void onLogon(const SessionID& sessionID) = 0;

    /**
     * @brief 会话登出回调
     * @param sessionID 即将断开的会话标识符
     *
     * 当 FIX 会话即将断开（收到 Logout 或连接关闭）时调用。
     * 可用于清理交易状态、取消未完成订单等。
     *
     * @note 可能被多个工作线程并发调用
     */
    virtual void onLogout(const SessionID& sessionID) = 0;

    // =========================================================================
    // 业务消息回调
    // =========================================================================

    /**
     * @brief 收到业务消息回调
     * @param msg 收到的 FIX 业务消息
     * @param sessionID 消息来源的会话标识符
     *
     * 当收到非会话层消息（MsgType 不是 A/0/1/5）时调用。
     * 典型的业务消息包括：
     * - D: NewOrderSingle（新订单）
     * - F: OrderCancelRequest（撤单请求）
     * - 8: ExecutionReport（执行报告）
     *
     * @warning 可能被多个工作线程并发调用！
     * 推荐实现：只做轻量操作，将消息 push 到无锁队列
     */
    virtual void fromApp(const FixMessage& msg, const SessionID& sessionID) = 0;

    /**
     * @brief 发送业务消息前回调
     * @param msg 即将发送的 FIX 业务消息（可修改）
     * @param sessionID 发送消息的会话标识符
     *
     * 在业务消息发送前调用，可用于：
     * - 记录审计日志
     * - 添加/修改字段
     * - 消息验证
     *
     * 默认实现为空，子类可选择性重写。
     *
     * @note 可能被多个工作线程并发调用
     */
    virtual void toApp(FixMessage& msg, const SessionID& sessionID) {
        (void)msg;
        (void)sessionID;
    }

    // =========================================================================
    // 管理消息回调（可选）
    // =========================================================================

    /**
     * @brief 收到管理消息回调
     * @param msg 收到的 FIX 管理消息
     * @param sessionID 消息来源的会话标识符
     *
     * 当收到会话层消息（Logon/Logout/Heartbeat/TestRequest）时调用。
     * 默认实现为空，子类可选择性重写用于日志记录等。
     *
     * @note 管理消息由 Session 自动处理，此回调仅用于通知
     */
    virtual void fromAdmin(const FixMessage& msg, const SessionID& sessionID) {
        (void)msg;
        (void)sessionID;
    }

    /**
     * @brief 发送管理消息前回调
     * @param msg 即将发送的 FIX 管理消息（可修改）
     * @param sessionID 发送消息的会话标识符
     *
     * 在发送 Logon/Logout/Heartbeat/TestRequest 前调用。
     * 默认实现为空，子类可选择性重写用于日志记录等。
     */
    virtual void toAdmin(FixMessage& msg, const SessionID& sessionID) {
        (void)msg;
        (void)sessionID;
    }
};

} // namespace fix40
