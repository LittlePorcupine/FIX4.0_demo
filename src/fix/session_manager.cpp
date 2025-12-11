/**
 * @file session_manager.cpp
 * @brief FIX 会话管理器实现
 */

#include "fix/session_manager.hpp"
#include "base/logger.hpp"

namespace fix40 {

void SessionManager::registerSession(std::shared_ptr<Session> session) {
    if (!session) {
        LOG() << "[SessionManager] Cannot register null session";
        return;
    }
    
    SessionID sessionID = session->get_session_id();
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = sessions_.emplace(sessionID, session);
    
    if (inserted) {
        LOG() << "[SessionManager] Registered session: " << sessionID.to_string();
    } else {
        // 替换旧会话
        it->second = session;
        LOG() << "[SessionManager] Replaced session: " << sessionID.to_string();
    }
}

bool SessionManager::unregisterSession(const SessionID& sessionID) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionID);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        LOG() << "[SessionManager] Unregistered session: " << sessionID.to_string();
        return true;
    }
    return false;
}

std::shared_ptr<Session> SessionManager::findSession(const SessionID& sessionID) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionID);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SessionManager::sendMessage(const SessionID& sessionID, FixMessage& msg) {
    std::shared_ptr<Session> session = findSession(sessionID);
    if (!session) {
        LOG() << "[SessionManager] Cannot send message: session not found " 
              << sessionID.to_string();
        return false;
    }
    
    if (!session->is_running()) {
        LOG() << "[SessionManager] Cannot send message: session not running "
              << sessionID.to_string();
        return false;
    }
    
    try {
        session->send_app_message(msg);
        return true;
    } catch (const std::exception& e) {
        LOG() << "[SessionManager] Exception sending message: " << e.what();
        return false;
    }
}

size_t SessionManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

bool SessionManager::hasSession(const SessionID& sessionID) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(sessionID) != sessions_.end();
}

void SessionManager::forEachSession(
    std::function<void(const SessionID&, std::shared_ptr<Session>)> callback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, session] : sessions_) {
        callback(id, session);
    }
}

} // namespace fix40
