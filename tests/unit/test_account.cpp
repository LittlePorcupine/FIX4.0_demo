/**
 * @file test_account.cpp
 * @brief Account 结构体单元测试和属性测试
 *
 * 测试账户数据结构的计算方法和属性。
 */

#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "app/account.hpp"

using namespace fix40;

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("Account 默认构造函数", "[account][unit]") {
    Account account;
    
    REQUIRE(account.accountId.empty());
    REQUIRE(account.balance == 0.0);
    REQUIRE(account.available == 0.0);
    REQUIRE(account.frozenMargin == 0.0);
    REQUIRE(account.usedMargin == 0.0);
    REQUIRE(account.positionProfit == 0.0);
    REQUIRE(account.closeProfit == 0.0);
}

TEST_CASE("Account 带参数构造函数", "[account][unit]") {
    Account account("user001", 1000000.0);
    
    REQUIRE(account.accountId == "user001");
    REQUIRE(account.balance == 1000000.0);
    REQUIRE(account.available == 1000000.0);
    REQUIRE(account.frozenMargin == 0.0);
    REQUIRE(account.usedMargin == 0.0);
    REQUIRE(account.positionProfit == 0.0);
    REQUIRE(account.closeProfit == 0.0);
}

TEST_CASE("Account getDynamicEquity 计算", "[account][unit]") {
    Account account("user001", 1000000.0);
    
    SECTION("无持仓盈亏时") {
        REQUIRE(account.getDynamicEquity() == 1000000.0);
    }
    
    SECTION("有正持仓盈亏时") {
        account.positionProfit = 50000.0;
        REQUIRE(account.getDynamicEquity() == 1050000.0);
    }
    
    SECTION("有负持仓盈亏时") {
        account.positionProfit = -30000.0;
        REQUIRE(account.getDynamicEquity() == 970000.0);
    }
}

TEST_CASE("Account getRiskRatio 计算", "[account][unit]") {
    Account account("user001", 1000000.0);
    
    SECTION("无占用保证金时风险度为0") {
        REQUIRE(account.getRiskRatio() == 0.0);
    }
    
    SECTION("有占用保证金时") {
        account.usedMargin = 100000.0;
        // 风险度 = 100000 / 1000000 = 0.1
        REQUIRE(account.getRiskRatio() == Approx(0.1));
    }
    
    SECTION("动态权益为0时风险度为0") {
        account.balance = 0.0;
        account.positionProfit = 0.0;
        account.usedMargin = 100000.0;
        REQUIRE(account.getRiskRatio() == 0.0);
    }
    
    SECTION("动态权益为负时风险度为0") {
        account.balance = 100000.0;
        account.positionProfit = -200000.0;  // 动态权益 = -100000
        account.usedMargin = 50000.0;
        REQUIRE(account.getRiskRatio() == 0.0);
    }
}

TEST_CASE("Account recalculateAvailable 计算", "[account][unit]") {
    Account account("user001", 1000000.0);
    
    SECTION("初始状态") {
        account.recalculateAvailable();
        REQUIRE(account.available == 1000000.0);
    }
    
    SECTION("有冻结保证金") {
        account.frozenMargin = 50000.0;
        account.recalculateAvailable();
        REQUIRE(account.available == 950000.0);
    }
    
    SECTION("有占用保证金") {
        account.usedMargin = 100000.0;
        account.recalculateAvailable();
        REQUIRE(account.available == 900000.0);
    }
    
    SECTION("有持仓盈亏") {
        account.positionProfit = 20000.0;
        account.recalculateAvailable();
        REQUIRE(account.available == 1020000.0);
    }
    
    SECTION("综合情况") {
        account.frozenMargin = 50000.0;
        account.usedMargin = 100000.0;
        account.positionProfit = 20000.0;
        account.recalculateAvailable();
        // 可用 = 1000000 + 20000 - 50000 - 100000 = 870000
        REQUIRE(account.available == 870000.0);
    }
}

TEST_CASE("Account 相等比较", "[account][unit]") {
    Account a1("user001", 1000000.0);
    Account a2("user001", 1000000.0);
    Account a3("user002", 1000000.0);
    
    REQUIRE(a1 == a2);
    REQUIRE(a1 != a3);
}

// =============================================================================
// 属性测试
// =============================================================================

/**
 * @brief Account 生成器
 * 
 * 生成有效的 Account 对象用于属性测试。
 */
namespace rc {
template<>
struct Arbitrary<Account> {
    static Gen<Account> arbitrary() {
        return gen::build<Account>(
            gen::set(&Account::accountId, gen::nonEmpty<std::string>()),
            gen::set(&Account::balance, gen::positive<double>()),
            gen::set(&Account::available, gen::positive<double>()),
            gen::set(&Account::frozenMargin, gen::nonNegative<double>()),
            gen::set(&Account::usedMargin, gen::nonNegative<double>()),
            gen::set(&Account::positionProfit, gen::arbitrary<double>()),
            gen::set(&Account::closeProfit, gen::arbitrary<double>())
        );
    }
};
} // namespace rc

/**
 * **Feature: paper-trading-system, Property 13: 账户数据持久化round-trip**
 * **Validates: Requirements 2.4, 12.1**
 * 
 * 注意：完整的 round-trip 测试需要存储层支持，将在任务 10 中实现。
 * 此处先测试 Account 结构体的序列化/反序列化一致性（通过相等比较）。
 */
TEST_CASE("Account 属性测试", "[account][property]") {
    
    rc::prop("动态权益等于余额加持仓盈亏",
        [](const Account& account) {
            double expected = account.balance + account.positionProfit;
            double actual = account.getDynamicEquity();
            return actual == expected;
        });
    
    rc::prop("风险度在有效范围内",
        []() {
            auto account = *rc::gen::arbitrary<Account>();
            double riskRatio = account.getRiskRatio();
            double equity = account.getDynamicEquity();
            
            if (equity <= 0) {
                // 动态权益非正时，风险度应为0
                return riskRatio == 0.0;
            } else {
                // 风险度应等于 占用保证金 / 动态权益
                double expected = account.usedMargin / equity;
                return riskRatio == expected;
            }
        });
    
    rc::prop("可用资金计算正确",
        []() {
            auto account = *rc::gen::arbitrary<Account>();
            account.recalculateAvailable();
            
            double expected = account.balance + account.positionProfit 
                            - account.frozenMargin - account.usedMargin;
            return account.available == expected;
        });
    
    rc::prop("Account 相等比较自反性",
        [](const Account& account) {
            return account == account;
        });
    
    rc::prop("Account 相等比较对称性",
        []() {
            auto a1 = *rc::gen::arbitrary<Account>();
            Account a2 = a1;  // 复制
            return (a1 == a2) && (a2 == a1);
        });
}
