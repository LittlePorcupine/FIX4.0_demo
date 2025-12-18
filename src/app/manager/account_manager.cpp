/**
 * @file account_manager.cpp
 * @brief 账户管理模块实现
 */

#include "app/manager/account_manager.hpp"
#include "storage/store.hpp"
#include <chrono>

namespace fix40 {

// =============================================================================
// 构造函数
// =============================================================================

AccountManager::AccountManager()
    : store_(nullptr)
{}

AccountManager::AccountManager(IStore* store)
    : store_(store)
{
    if (store_) {
        // 启动时从存储恢复账户状态（用于服务端重启后资金连续性）。
        // 若存储不可用/为空，则保持内存态为空，由后续登录自动开户。
        auto accounts = store_->loadAllAccounts();
        std::lock_guard<std::mutex> lock(mutex_);
        accounts_.clear();
        for (const auto& account : accounts) {
            if (!account.accountId.empty()) {
                accounts_[account.accountId] = account;
            }
        }
    }
}

// =============================================================================
// 账户管理
// =============================================================================

Account AccountManager::createAccount(const std::string& accountId, double initialBalance) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查账户是否已存在
    auto it = accounts_.find(accountId);
    if (it != accounts_.end()) {
        return it->second;
    }
    
    // 创建新账户
    Account account(accountId, initialBalance);
    accounts_[accountId] = account;
    
    // 持久化
    persistAccount(account);
    
    return account;
}

std::optional<Account> AccountManager::getAccount(const std::string& accountId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it != accounts_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool AccountManager::hasAccount(const std::string& accountId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_.find(accountId) != accounts_.end();
}

std::vector<std::string> AccountManager::getAllAccountIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> ids;
    ids.reserve(accounts_.size());
    for (const auto& pair : accounts_) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t AccountManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_.size();
}

// =============================================================================
// 保证金操作
// =============================================================================

bool AccountManager::freezeMargin(const std::string& accountId, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 检查可用资金是否足够
    if (account.available < amount) {
        return false;
    }
    
    // 冻结保证金
    account.available -= amount;
    account.frozenMargin += amount;
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

bool AccountManager::unfreezeMargin(const std::string& accountId, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 释放冻结保证金
    account.frozenMargin -= amount;
    account.available += amount;
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

bool AccountManager::confirmMargin(const std::string& accountId, 
                                    double frozenAmount, double usedAmount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 冻结转占用
    account.frozenMargin -= frozenAmount;
    account.usedMargin += usedAmount;
    
    // 多冻结的部分返还到可用资金
    double diff = frozenAmount - usedAmount;
    if (diff > 0) {
        account.available += diff;
    }
    
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

bool AccountManager::releaseMargin(const std::string& accountId, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 释放占用保证金
    account.usedMargin -= amount;
    account.available += amount;
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

// =============================================================================
// 盈亏操作
// =============================================================================

bool AccountManager::updatePositionProfit(const std::string& accountId, double profit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 计算盈亏变化量
    double profitDelta = profit - account.positionProfit;
    
    // 更新持仓盈亏和可用资金
    account.positionProfit = profit;
    account.available += profitDelta;
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

bool AccountManager::addCloseProfit(const std::string& accountId, double profit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = accounts_.find(accountId);
    if (it == accounts_.end()) {
        return false;
    }
    
    Account& account = it->second;
    
    // 记录平仓盈亏
    account.balance += profit;
    account.closeProfit += profit;
    account.available += profit;
    account.updateTime = std::chrono::system_clock::now();
    
    // 持久化
    persistAccount(account);
    
    return true;
}

// =============================================================================
// 清理方法
// =============================================================================

void AccountManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    accounts_.clear();
}

// =============================================================================
// 私有方法
// =============================================================================

void AccountManager::persistAccount(const Account& account) {
    if (!store_) return;
    // 注意：此处为 best-effort 持久化。存储失败时不抛异常，以免影响撮合主流程。
    store_->saveAccount(account);
}

} // namespace fix40
