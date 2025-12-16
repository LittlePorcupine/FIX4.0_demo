/**
 * @file test_account_manager.cpp
 * @brief AccountManager 单元测试和属性测试
 *
 * 测试账户管理器的创建、查询、资金冻结/释放功能。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/account_manager.hpp"
#include <cmath>

using namespace fix40;

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("AccountManager 默认构造", "[account_manager][unit]") {
    AccountManager mgr;
    
    REQUIRE(mgr.size() == 0);
    REQUIRE(mgr.getAllAccountIds().empty());
}

TEST_CASE("AccountManager createAccount 创建账户", "[account_manager][unit]") {
    AccountManager mgr;
    
    Account account = mgr.createAccount("user001", 1000000.0);
    
    REQUIRE(account.accountId == "user001");
    REQUIRE(account.balance == 1000000.0);
    REQUIRE(account.available == 1000000.0);
    REQUIRE(account.frozenMargin == 0.0);
    REQUIRE(account.usedMargin == 0.0);
    REQUIRE(mgr.size() == 1);
}

TEST_CASE("AccountManager createAccount 重复创建返回现有账户", "[account_manager][unit]") {
    AccountManager mgr;
    
    Account account1 = mgr.createAccount("user001", 1000000.0);
    Account account2 = mgr.createAccount("user001", 2000000.0);  // 不同金额
    
    REQUIRE(mgr.size() == 1);
    REQUIRE(account2.balance == 1000000.0);  // 应该返回原账户
}

TEST_CASE("AccountManager getAccount 获取账户", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    SECTION("获取存在的账户") {
        auto account = mgr.getAccount("user001");
        REQUIRE(account.has_value());
        REQUIRE(account->accountId == "user001");
        REQUIRE(account->balance == 1000000.0);
    }
    
    SECTION("获取不存在的账户") {
        auto account = mgr.getAccount("unknown");
        REQUIRE(!account.has_value());
    }
}

TEST_CASE("AccountManager hasAccount 检查账户存在", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    REQUIRE(mgr.hasAccount("user001") == true);
    REQUIRE(mgr.hasAccount("unknown") == false);
}

TEST_CASE("AccountManager freezeMargin 冻结保证金", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    SECTION("正常冻结") {
        bool result = mgr.freezeMargin("user001", 100000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->available == 900000.0);
        REQUIRE(account->frozenMargin == 100000.0);
    }
    
    SECTION("资金不足") {
        bool result = mgr.freezeMargin("user001", 2000000.0);
        REQUIRE(result == false);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->available == 1000000.0);  // 未变化
        REQUIRE(account->frozenMargin == 0.0);
    }
    
    SECTION("账户不存在") {
        bool result = mgr.freezeMargin("unknown", 100000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager unfreezeMargin 释放冻结保证金", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    mgr.freezeMargin("user001", 100000.0);
    
    SECTION("正常释放") {
        bool result = mgr.unfreezeMargin("user001", 100000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->available == 1000000.0);
        REQUIRE(account->frozenMargin == 0.0);
    }
    
    SECTION("部分释放") {
        bool result = mgr.unfreezeMargin("user001", 50000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->available == 950000.0);
        REQUIRE(account->frozenMargin == 50000.0);
    }
    
    SECTION("账户不存在") {
        bool result = mgr.unfreezeMargin("unknown", 100000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager confirmMargin 确认保证金", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    mgr.freezeMargin("user001", 100000.0);
    
    SECTION("冻结金额等于占用金额") {
        bool result = mgr.confirmMargin("user001", 100000.0, 100000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->frozenMargin == 0.0);
        REQUIRE(account->usedMargin == 100000.0);
        REQUIRE(account->available == 900000.0);  // 不变
    }
    
    SECTION("冻结金额大于占用金额（多冻结的返还）") {
        bool result = mgr.confirmMargin("user001", 100000.0, 80000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->frozenMargin == 0.0);
        REQUIRE(account->usedMargin == 80000.0);
        REQUIRE(account->available == 920000.0);  // 返还20000
    }
    
    SECTION("账户不存在") {
        bool result = mgr.confirmMargin("unknown", 100000.0, 100000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager releaseMargin 释放占用保证金", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    mgr.freezeMargin("user001", 100000.0);
    mgr.confirmMargin("user001", 100000.0, 100000.0);
    
    SECTION("正常释放") {
        bool result = mgr.releaseMargin("user001", 100000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->usedMargin == 0.0);
        REQUIRE(account->available == 1000000.0);
    }
    
    SECTION("部分释放") {
        bool result = mgr.releaseMargin("user001", 50000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->usedMargin == 50000.0);
        REQUIRE(account->available == 950000.0);
    }
    
    SECTION("账户不存在") {
        bool result = mgr.releaseMargin("unknown", 100000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager updatePositionProfit 更新持仓盈亏", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    SECTION("盈利") {
        bool result = mgr.updatePositionProfit("user001", 50000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->positionProfit == 50000.0);
        REQUIRE(account->available == 1050000.0);
    }
    
    SECTION("亏损") {
        bool result = mgr.updatePositionProfit("user001", -30000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->positionProfit == -30000.0);
        REQUIRE(account->available == 970000.0);
    }
    
    SECTION("盈亏变化") {
        mgr.updatePositionProfit("user001", 50000.0);
        mgr.updatePositionProfit("user001", 30000.0);  // 盈利减少
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->positionProfit == 30000.0);
        REQUIRE(account->available == 1030000.0);
    }
    
    SECTION("账户不存在") {
        bool result = mgr.updatePositionProfit("unknown", 50000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager addCloseProfit 记录平仓盈亏", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    SECTION("盈利") {
        bool result = mgr.addCloseProfit("user001", 50000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->balance == 1050000.0);
        REQUIRE(account->closeProfit == 50000.0);
        REQUIRE(account->available == 1050000.0);
    }
    
    SECTION("亏损") {
        bool result = mgr.addCloseProfit("user001", -30000.0);
        REQUIRE(result == true);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->balance == 970000.0);
        REQUIRE(account->closeProfit == -30000.0);
        REQUIRE(account->available == 970000.0);
    }
    
    SECTION("累计盈亏") {
        mgr.addCloseProfit("user001", 50000.0);
        mgr.addCloseProfit("user001", -20000.0);
        
        auto account = mgr.getAccount("user001");
        REQUIRE(account->balance == 1030000.0);
        REQUIRE(account->closeProfit == 30000.0);
    }
    
    SECTION("账户不存在") {
        bool result = mgr.addCloseProfit("unknown", 50000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("AccountManager clear 清空账户", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    mgr.createAccount("user002", 2000000.0);
    
    REQUIRE(mgr.size() == 2);
    
    mgr.clear();
    
    REQUIRE(mgr.size() == 0);
    REQUIRE(!mgr.hasAccount("user001"));
    REQUIRE(!mgr.hasAccount("user002"));
}

TEST_CASE("AccountManager 完整交易流程", "[account_manager][unit]") {
    AccountManager mgr;
    mgr.createAccount("user001", 1000000.0);
    
    // 1. 下单冻结保证金
    REQUIRE(mgr.freezeMargin("user001", 100000.0) == true);
    
    auto account = mgr.getAccount("user001");
    REQUIRE(account->available == 900000.0);
    REQUIRE(account->frozenMargin == 100000.0);
    
    // 2. 成交确认保证金
    REQUIRE(mgr.confirmMargin("user001", 100000.0, 100000.0) == true);
    
    account = mgr.getAccount("user001");
    REQUIRE(account->frozenMargin == 0.0);
    REQUIRE(account->usedMargin == 100000.0);
    
    // 3. 持仓盈利
    REQUIRE(mgr.updatePositionProfit("user001", 20000.0) == true);
    
    account = mgr.getAccount("user001");
    REQUIRE(account->positionProfit == 20000.0);
    REQUIRE(account->available == 920000.0);
    
    // 4. 平仓
    REQUIRE(mgr.releaseMargin("user001", 100000.0) == true);
    REQUIRE(mgr.addCloseProfit("user001", 20000.0) == true);
    REQUIRE(mgr.updatePositionProfit("user001", 0.0) == true);  // 清除浮动盈亏
    
    account = mgr.getAccount("user001");
    REQUIRE(account->balance == 1020000.0);
    REQUIRE(account->available == 1020000.0);
    REQUIRE(account->usedMargin == 0.0);
    REQUIRE(account->positionProfit == 0.0);
    REQUIRE(account->closeProfit == 20000.0);
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * **Feature: paper-trading-system, Property 6: 保证金生命周期一致性**
 * **Validates: Requirements 8.1, 8.2, 8.3, 8.4**
 *
 * 对于任意订单的完整生命周期（下单→成交/撤销），
 * 冻结保证金和占用保证金的变化应保持一致。
 */
TEST_CASE("AccountManager 属性测试", "[account_manager][property]") {
    
    rc::prop("冻结后释放应恢复原状态",
        []() {
            AccountManager mgr;
            
            // 生成有效的初始余额和冻结金额
            auto balance = *rc::gen::inRange(100000, 10000000);
            auto freezeRatio = *rc::gen::inRange(1, 99);  // 1-99%
            double freezeAmount = balance * freezeRatio / 100.0;
            
            mgr.createAccount("test", static_cast<double>(balance));
            
            // 冻结
            bool frozeOk = mgr.freezeMargin("test", freezeAmount);
            RC_ASSERT(frozeOk);
            
            // 释放
            bool unfrozeOk = mgr.unfreezeMargin("test", freezeAmount);
            RC_ASSERT(unfrozeOk);
            
            // 验证恢复原状态
            auto account = mgr.getAccount("test");
            RC_ASSERT(account.has_value());
            RC_ASSERT(std::abs(account->available - balance) < 0.01);
            RC_ASSERT(std::abs(account->frozenMargin) < 0.01);
        });
    
    rc::prop("冻结转占用后释放应恢复原状态",
        []() {
            AccountManager mgr;
            
            auto balance = *rc::gen::inRange(100000, 10000000);
            auto freezeRatio = *rc::gen::inRange(1, 99);
            double freezeAmount = balance * freezeRatio / 100.0;
            
            mgr.createAccount("test", static_cast<double>(balance));
            
            // 冻结
            bool frozeOk = mgr.freezeMargin("test", freezeAmount);
            RC_ASSERT(frozeOk);
            
            // 确认（冻结转占用）
            bool confirmedOk = mgr.confirmMargin("test", freezeAmount, freezeAmount);
            RC_ASSERT(confirmedOk);
            
            auto afterConfirm = mgr.getAccount("test");
            RC_ASSERT(std::abs(afterConfirm->frozenMargin) < 0.01);
            RC_ASSERT(std::abs(afterConfirm->usedMargin - freezeAmount) < 0.01);
            
            // 释放占用
            bool releasedOk = mgr.releaseMargin("test", freezeAmount);
            RC_ASSERT(releasedOk);
            
            // 验证恢复原状态
            auto account = mgr.getAccount("test");
            RC_ASSERT(std::abs(account->available - balance) < 0.01);
            RC_ASSERT(std::abs(account->usedMargin) < 0.01);
        });
    
    rc::prop("资金不足时冻结应失败",
        []() {
            AccountManager mgr;
            
            auto balance = *rc::gen::inRange(10000, 100000);
            auto excessRatio = *rc::gen::inRange(101, 200);  // 101-200%
            double freezeAmount = balance * excessRatio / 100.0;
            
            mgr.createAccount("test", static_cast<double>(balance));
            
            // 尝试冻结超过余额的金额
            bool result = mgr.freezeMargin("test", freezeAmount);
            RC_ASSERT(!result);
            
            // 验证账户未变化
            auto account = mgr.getAccount("test");
            RC_ASSERT(std::abs(account->available - balance) < 0.01);
            RC_ASSERT(std::abs(account->frozenMargin) < 0.01);
        });
    
    rc::prop("多次冻结后总冻结金额正确",
        []() {
            AccountManager mgr;
            
            auto balance = *rc::gen::inRange(1000000, 10000000);
            auto numFreezes = *rc::gen::inRange(1, 10);
            
            mgr.createAccount("test", static_cast<double>(balance));
            
            double totalFrozen = 0.0;
            double maxPerFreeze = balance / (numFreezes + 1);  // 确保不会超额
            
            for (int i = 0; i < numFreezes; ++i) {
                auto freezeRatio = *rc::gen::inRange(1, 50);
                double freezeAmount = maxPerFreeze * freezeRatio / 100.0;
                
                if (mgr.freezeMargin("test", freezeAmount)) {
                    totalFrozen += freezeAmount;
                }
            }
            
            auto account = mgr.getAccount("test");
            RC_ASSERT(std::abs(account->frozenMargin - totalFrozen) < 0.01);
            RC_ASSERT(std::abs(account->available - (balance - totalFrozen)) < 0.01);
        });
    
    rc::prop("平仓盈亏应正确累加到余额",
        []() {
            AccountManager mgr;
            
            auto balance = *rc::gen::inRange(100000, 10000000);
            auto profitInt = *rc::gen::inRange(-50000, 50000);
            double profit = static_cast<double>(profitInt);
            
            mgr.createAccount("test", static_cast<double>(balance));
            
            mgr.addCloseProfit("test", profit);
            
            auto account = mgr.getAccount("test");
            RC_ASSERT(std::abs(account->balance - (balance + profit)) < 0.01);
            RC_ASSERT(std::abs(account->closeProfit - profit) < 0.01);
        });
}
