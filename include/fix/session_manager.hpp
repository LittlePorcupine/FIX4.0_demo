/**
 * @file session_manager.hpp
 * @brief FIX 会话管理器
 *
 * 管理所有活跃的 FIX 会话，提供会话查找和消息发送功能。
 * 用于将 MatchingEngine 产生的 ExecutionReport 发送到正确的客户端。
 */

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "fix/session.hpp"
#include "fix/application.hpp"

namespace fix40 {

/**
 * @brief SessionID 的哈希函数
 */
struct SessionIDHash {
    std::size_t operator()(const SessionID& id) const {
        return std::hash<std::string>()(id.senderCompID) ^
               (std::hash<std::string>()(id.targetCompID) << 1);
    }
};

/**
 * @class SessionManager
 * @brief FIX 会话管理器
 *
 * 线程安全地管理所有活跃的 FIX 会话，提供：
 * - 会话注册/注销
 * - 按 SessionID 查找会话
 * - 向指定会话发送消息
 *
 * @par 使用场景
 * MatchingEngine 产生 ExecutionReport 后，通过 SessionManager 
 * 找到对应的 Session 并发送消息。
 *
 * @par 线程安全
 * 所有公共方法都是线程安全的。
 *
 * @par 使用示例
 * @code
 * SessionManager manager;
 * 
 * // 注册会话
 * manager.registerSession(session);
 * 
 * // 发送消息
 * FixMessage report;
 * // ... 构建 ExecutionReport ...
 * manager.sendMessage(sessionID, report);
 * 
 * // 注销会话
 * manager.unregisterSession(sessionID);
 * @endcode
 */
class SessionManager {
public:
    /**
     * @brief 构造会话管理器
     */
    SessionManager() = default;

    /**
     * @brief 析构函数
     */
    ~SessionManager() = default;

    // 禁止拷贝
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /**
     * @brief 注册会话
     * @param session 会话对象
     *
     * 将会话添加到管理器中。如果已存在相同 SessionID 的会话，
     * 会替换旧的会话。
     */
    void registerSession(std::shared_ptr<Session> session);

    /**
     * @brief 注销会话
     * @param sessionID 会话标识符
     * @return true 成功注销
     * @return false 会话不存在
     */
    bool unregisterSession(const SessionID& sessionID);

    /**
     * @brief 查找会话
     * @param sessionID 会话标识符
     * @return std::shared_ptr<Session> 会话对象，不存在返回 nullptr
     */
    std::shared_ptr<Session> findSession(const SessionID& sessionID) const;

    /**
     * @brief 向指定会话发送消息
     * @param sessionID 目标会话标识符
     * @param msg 要发送的 FIX 消息
     * @return true 发送成功
     * @return false 会话不存在或发送失败
     *
     * 此方法会调用 Session::send_app_message()，
     * 触发 Application::toApp() 回调。
     */
    bool sendMessage(const SessionID& sessionID, FixMessage& msg);

    /**
     * @brief 获取活跃会话数量
     */
    size_t getSessionCount() const;

    /**
     * @brief 检查会话是否存在
     * @param sessionID 会话标识符
     */
    bool hasSession(const SessionID& sessionID) const;

    /**
     * @brief 遍历所有会话
     * @param callback 回调函数，参数为 SessionID 和 Session
     */
    void forEachSession(std::function<void(const SessionID&, std::shared_ptr<Session>)> callback) const;

private:
    mutable std::mutex mutex_;  ///< 保护 sessions_ 的互斥锁
    std::unordered_map<SessionID, std::shared_ptr<Session>, SessionIDHash> sessions_;
};

} // namespace fix40
