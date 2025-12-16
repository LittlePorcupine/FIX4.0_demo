/**
 * @file risk_manager.cpp
 * @brief 风险控制模块实现
 *
 * 实现订单风控检查功能。
 */

#include "app/risk_manager.hpp"
#include <sstream>

namespace fix40 {

// =============================================================================
// 主检查方法
// =============================================================================

CheckResult RiskManager::checkOrder(
    const Order& order,
    const Account& account,
    const Position& position,
    const Instrument& instrument,
    const MarketDataSnapshot& snapshot,
    OffsetFlag offsetFlag
) const {
    // 1. 价格检查（限价单）
    if (order.ordType == OrderType::LIMIT) {
        CheckResult priceResult = checkPrice(order, instrument);
        if (!priceResult.passed) {
            return priceResult;
        }
    }

    // 2. 资金检查（开仓）
    if (offsetFlag == OffsetFlag::OPEN) {
        CheckResult marginResult = checkMargin(order, account, instrument);
        if (!marginResult.passed) {
            return marginResult;
        }
    }

    // 3. 持仓检查（平仓）
    if (offsetFlag == OffsetFlag::CLOSE) {
        CheckResult positionResult = checkPosition(order, position);
        if (!positionResult.passed) {
            return positionResult;
        }
    }

    // 4. 对手盘检查（市价单）
    if (order.ordType == OrderType::MARKET) {
        CheckResult counterPartyResult = checkCounterParty(order, snapshot);
        if (!counterPartyResult.passed) {
            return counterPartyResult;
        }
    }

    return CheckResult::success();
}

// =============================================================================
// 子检查方法
// =============================================================================

CheckResult RiskManager::checkMargin(
    const Order& order,
    const Account& account,
    const Instrument& instrument
) const {
    double requiredMargin = calculateRequiredMargin(order, instrument);
    
    if (account.available < requiredMargin) {
        std::ostringstream oss;
        oss << "Insufficient funds: required=" << requiredMargin
            << ", available=" << account.available;
        return CheckResult::failure(RejectReason::INSUFFICIENT_FUNDS, oss.str());
    }
    
    return CheckResult::success();
}

CheckResult RiskManager::checkPrice(
    const Order& order,
    const Instrument& instrument
) const {
    // 市价单不检查价格
    if (order.ordType == OrderType::MARKET) {
        return CheckResult::success();
    }
    
    // 如果涨跌停价格未设置，跳过检查
    if (instrument.lowerLimitPrice <= 0 && instrument.upperLimitPrice <= 0) {
        return CheckResult::success();
    }
    
    // 检查价格是否在涨跌停范围内
    if (!instrument.isPriceValid(order.price)) {
        std::ostringstream oss;
        oss << "Price out of limit: price=" << order.price
            << ", lower=" << instrument.lowerLimitPrice
            << ", upper=" << instrument.upperLimitPrice;
        return CheckResult::failure(RejectReason::PRICE_OUT_OF_LIMIT, oss.str());
    }
    
    return CheckResult::success();
}

CheckResult RiskManager::checkPosition(
    const Order& order,
    const Position& position
) const {
    // 买单平空头，卖单平多头
    int64_t availablePosition = 0;
    
    if (order.side == OrderSide::BUY) {
        // 买入平仓 = 平空头
        availablePosition = position.shortPosition;
    } else {
        // 卖出平仓 = 平多头
        availablePosition = position.longPosition;
    }
    
    if (order.orderQty > availablePosition) {
        std::ostringstream oss;
        oss << "Insufficient position: required=" << order.orderQty
            << ", available=" << availablePosition;
        return CheckResult::failure(RejectReason::INSUFFICIENT_POSITION, oss.str());
    }
    
    return CheckResult::success();
}

CheckResult RiskManager::checkCounterParty(
    const Order& order,
    const MarketDataSnapshot& snapshot
) const {
    // 限价单不检查对手盘
    if (order.ordType == OrderType::LIMIT) {
        return CheckResult::success();
    }
    
    bool hasCounterParty = false;
    
    if (order.side == OrderSide::BUY) {
        // 买单需要有卖盘
        hasCounterParty = snapshot.hasAsk();
    } else {
        // 卖单需要有买盘
        hasCounterParty = snapshot.hasBid();
    }
    
    if (!hasCounterParty) {
        std::string sideStr = (order.side == OrderSide::BUY) ? "ask" : "bid";
        std::ostringstream oss;
        oss << "No counter party: no " << sideStr << " available";
        return CheckResult::failure(RejectReason::NO_COUNTER_PARTY, oss.str());
    }
    
    return CheckResult::success();
}

// =============================================================================
// 辅助方法
// =============================================================================

double RiskManager::calculateRequiredMargin(
    const Order& order,
    const Instrument& instrument
) const {
    double price = order.price;
    
    // 市价单使用涨停价（买单）或跌停价（卖单）计算保证金
    if (order.ordType == OrderType::MARKET) {
        if (order.side == OrderSide::BUY) {
            // 买单使用涨停价
            price = instrument.upperLimitPrice;
            // 如果涨停价未设置，使用昨结算价
            if (price <= 0) {
                price = instrument.preSettlementPrice;
            }
        } else {
            // 卖单使用跌停价
            price = instrument.lowerLimitPrice;
            // 如果跌停价未设置，使用昨结算价
            if (price <= 0) {
                price = instrument.preSettlementPrice;
            }
        }
    }
    
    return instrument.calculateMargin(price, order.orderQty);
}

} // namespace fix40
