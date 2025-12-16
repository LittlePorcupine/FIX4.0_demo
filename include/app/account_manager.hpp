/**
 * @file account_manager.hpp
 * @brief 账户管理模块
 *
 * 提供账户的创建、查询、资金冻结/释放等功能。
 * 支持与存储层集成进行持久化。
 */

#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <vector>
#include "app/account.hpp"

namespace fix40 {

// 前向声明
class IStore;

/**
 * @class AccountManager
 * @brief 账户管理器
 *
 * 负责账户的创建、查询、资金冻结/释放等操作。
 * 支持与IStore接口集成进行数据持久化。
 *
 * @par 线程安全
 * 所有公共方法都是线程安全的，使用互斥锁保护内部数据。
 *
 * @par 保证金生命周期
 * 1. 下单时：freezeMargin() - 冻结保证金
 * 2. 成交时：confirmMargin() - 冻结转占用
 * 3. 撤单/拒绝时：unfreezeMargin() - 释放冻结
 * 4. 平仓时：releaseMargin() - 释放占用
 *
 * @par 使用示例
 * @code
 * AccountManager mgr;
 * 
 * // 创建账户
 * auto account = mgr.createAccount("user001", 1000000.0);
 * 
 * // 下单时冻结保证金
 * mgr.freezeMargin("user001", 50000.0);
 * 
 * // 成交时转为占用保证金
 * mgr.confirmMargin("user001", 50000.0, 50000.0);
 * 
 * // 平仓时释放占用保证金
 * mgr.releaseMargin("user001", 50000.0);
 * @endcode
 */
class AccountManager {
public:
    // -------------------------------------------------------------------------
    // 构造函数
    // -------------------------------------------------------------------------

    /**
     * @brief 默认构造函数
     *
     * 创建不带持久化的账户管理器。
     */
    AccountManager();

    /**
     * @brief 带存储接口的构造函数
     *
     * @param store 存储接口指针（可为nullptr）
     */
    explicit AccountManager(IStore* store);

    /**
     * @brief 析构函数
     */
    ~AccountManager() = default;

    // 禁用拷贝
    AccountManager(const AccountManager&) = delete;
    AccountManager& operator=(const AccountManager&) = delete;

    // -------------------------------------------------------------------------
    // 账户管理
    // -------------------------------------------------------------------------

    /**
     * @brief 创建账户
     *
     * 创建一个新的虚拟账户，初始余额和可用资金相等。
     *
     * @param accountId 账户ID
     * @param initialBalance 初始余额
     * @return 创建的账户
     *
     * @note 如果账户已存在，返回现有账户
     */
    Account createAccount(const std::string& accountId, double initialBalance);

    /**
     * @brief 获取账户
     *
     * @param accountId 账户ID
     * @return 账户信息，不存在时返回 std::nullopt
     */
    std::optional<Account> getAccount(const std::string& accountId) const;

    /**
     * @brief 检查账户是否存在
     *
     * @param accountId 账户ID
     * @return 存在返回 true
     */
    bool hasAccount(const std::string& accountId) const;

    /**
     * @brief 获取所有账户ID
     *
     * @return 账户ID列表
     */
    std::vector<std::string> getAllAccountIds() const;

    /**
     * @brief 获取账户数量
     *
     * @return 账户数量
     */
    size_t size() const;

    // -------------------------------------------------------------------------
    // 保证金操作
    // -------------------------------------------------------------------------

    /**
     * @brief 冻结保证金（下单时）
     *
     * 从可用资金中冻结指定金额作为挂单保证金。
     *
     * @param accountId 账户ID
     * @param amount 冻结金额
     * @return 成功返回 true，资金不足或账户不存在返回 false
     *
     * @par 资金变化
     * - available -= amount
     * - frozenMargin += amount
     */
    bool freezeMargin(const std::string& accountId, double amount);

    /**
     * @brief 释放冻结保证金（撤单/拒绝时）
     *
     * 将冻结的保证金释放回可用资金。
     *
     * @param accountId 账户ID
     * @param amount 释放金额
     * @return 成功返回 true，账户不存在返回 false
     *
     * @par 资金变化
     * - available += amount
     * - frozenMargin -= amount
     */
    bool unfreezeMargin(const std::string& accountId, double amount);

    /**
     * @brief 确认保证金（成交时：冻结转占用）
     *
     * 将冻结的保证金转为占用保证金。
     *
     * @param accountId 账户ID
     * @param frozenAmount 原冻结金额
     * @param usedAmount 实际占用金额（可能与冻结金额不同）
     * @return 成功返回 true，账户不存在返回 false
     *
     * @par 资金变化
     * - frozenMargin -= frozenAmount
     * - usedMargin += usedAmount
     * - available += (frozenAmount - usedAmount)  // 多冻结的部分返还
     */
    bool confirmMargin(const std::string& accountId, double frozenAmount, double usedAmount);

    /**
     * @brief 释放占用保证金（平仓时）
     *
     * 平仓后释放占用的保证金。
     *
     * @param accountId 账户ID
     * @param amount 释放金额
     * @return 成功返回 true，账户不存在返回 false
     *
     * @par 资金变化
     * - usedMargin -= amount
     * - available += amount
     */
    bool releaseMargin(const std::string& accountId, double amount);

    // -------------------------------------------------------------------------
    // 盈亏操作
    // -------------------------------------------------------------------------

    /**
     * @brief 更新持仓盈亏
     *
     * 更新账户的浮动盈亏，同时更新可用资金。
     *
     * @param accountId 账户ID
     * @param profit 新的持仓盈亏值（不是增量）
     * @return 成功返回 true，账户不存在返回 false
     */
    bool updatePositionProfit(const std::string& accountId, double profit);

    /**
     * @brief 记录平仓盈亏
     *
     * 平仓时记录已实现盈亏，更新余额。
     *
     * @param accountId 账户ID
     * @param profit 平仓盈亏（正为盈利，负为亏损）
     * @return 成功返回 true，账户不存在返回 false
     *
     * @par 资金变化
     * - balance += profit
     * - closeProfit += profit
     * - available += profit
     */
    bool addCloseProfit(const std::string& accountId, double profit);

    // -------------------------------------------------------------------------
    // 清理方法
    // -------------------------------------------------------------------------

    /**
     * @brief 清空所有账户
     */
    void clear();

private:
    /**
     * @brief 持久化账户（内部方法）
     *
     * @param account 要持久化的账户
     */
    void persistAccount(const Account& account);

    /// 账户映射表：accountId -> Account
    std::unordered_map<std::string, Account> accounts_;

    /// 存储接口（可为nullptr）
    IStore* store_;

    /// 互斥锁，保护 accounts_
    mutable std::mutex mutex_;
};

} // namespace fix40
