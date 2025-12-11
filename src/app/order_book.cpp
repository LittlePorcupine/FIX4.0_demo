/**
 * @file order_book.cpp
 * @brief 订单簿实现
 */

#include "app/order_book.hpp"
#include "base/logger.hpp"
#include <sstream>
#include <iomanip>

namespace fix40 {

OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol)
{
}

std::string OrderBook::generateOrderID() {
    std::ostringstream oss;
    oss << symbol_ << "-" << std::setfill('0') << std::setw(8) << nextOrderID_++;
    return oss.str();
}

std::string OrderBook::generateTradeID() {
    std::ostringstream oss;
    oss << symbol_ << "-T" << std::setfill('0') << std::setw(8) << nextTradeID_++;
    return oss.str();
}

std::vector<Trade> OrderBook::addOrder(Order& order) {
    // 输入校验
    if (order.orderQty <= 0) {
        LOG() << "[OrderBook:" << symbol_ << "] Rejected: invalid orderQty " << order.orderQty;
        order.status = OrderStatus::REJECTED;
        return {};
    }
    if (order.leavesQty <= 0) {
        LOG() << "[OrderBook:" << symbol_ << "] Rejected: invalid leavesQty " << order.leavesQty;
        order.status = OrderStatus::REJECTED;
        return {};
    }
    if (order.ordType == OrderType::LIMIT && order.price <= 0) {
        LOG() << "[OrderBook:" << symbol_ << "] Rejected: invalid price " << order.price << " for limit order";
        order.status = OrderStatus::REJECTED;
        return {};
    }
    if (order.symbol != symbol_) {
        LOG() << "[OrderBook:" << symbol_ << "] Rejected: symbol mismatch " << order.symbol;
        order.status = OrderStatus::REJECTED;
        return {};
    }
    if (order.clOrdID.empty()) {
        LOG() << "[OrderBook:" << symbol_ << "] Rejected: empty clOrdID";
        order.status = OrderStatus::REJECTED;
        return {};
    }
    
    // 市价单检查：必须有对手盘
    if (order.ordType == OrderType::MARKET) {
        bool hasCounterparty = (order.side == OrderSide::BUY) ? !asks_.empty() : !bids_.empty();
        if (!hasCounterparty) {
            LOG() << "[OrderBook:" << symbol_ << "] Rejected: market order with no counterparty";
            order.status = OrderStatus::REJECTED;
            return {};
        }
    }
    
    // 生成订单ID
    order.orderID = generateOrderID();
    order.updateTime = std::chrono::system_clock::now();
    
    const char* ordTypeStr = (order.ordType == OrderType::MARKET) ? "MARKET" : "LIMIT";
    LOG() << "[OrderBook:" << symbol_ << "] Adding " << ordTypeStr << " order: " 
          << order.clOrdID << " " << (order.side == OrderSide::BUY ? "BUY" : "SELL")
          << " " << order.orderQty 
          << (order.ordType == OrderType::LIMIT ? " @ " + std::to_string(order.price) : "");

    // FOK 订单预检查：必须能全部成交
    if (order.timeInForce == TimeInForce::FOK) {
        int64_t availableQty = calculateMatchableQty(order);
        if (availableQty < order.orderQty) {
            LOG() << "[OrderBook:" << symbol_ << "] FOK order " << order.clOrdID 
                  << " rejected: available " << availableQty << " < required " << order.orderQty;
            order.status = OrderStatus::REJECTED;
            return {};
        }
    }

    std::vector<Trade> trades;
    
    // 根据买卖方向撮合
    if (order.side == OrderSide::BUY) {
        trades = matchBuyOrder(order);
    } else {
        trades = matchSellOrder(order);
    }

    // 处理剩余数量
    if (order.leavesQty > 0 && !order.isTerminal()) {
        bool shouldCancelRemaining = false;
        const char* cancelReason = nullptr;
        
        if (order.ordType == OrderType::MARKET) {
            // 市价单：未成交部分取消
            shouldCancelRemaining = true;
            cancelReason = "no more counterparty";
        } else if (order.timeInForce == TimeInForce::IOC) {
            // IOC: 立即成交否则取消，未成交部分取消
            shouldCancelRemaining = true;
            cancelReason = "IOC order";
        } else if (order.timeInForce == TimeInForce::FOK) {
            // FOK: 不应该走到这里，因为 FOK 在前面已经处理
            // 如果走到这里说明有部分成交，这是错误的
            shouldCancelRemaining = true;
            cancelReason = "FOK partial fill (should not happen)";
        } else {
            // DAY/GTC: 限价单挂入订单簿
            if (order.side == OrderSide::BUY) {
                addToBids(order);
            } else {
                addToAsks(order);
            }
            
            // 更新订单状态
            if (order.cumQty == 0) {
                order.status = OrderStatus::NEW;
            }
            // 部分成交状态在撮合时已设置
        }
        
        if (shouldCancelRemaining) {
            bool partialFilled = (order.cumQty > 0);
            order.status = partialFilled ? OrderStatus::CANCELED : OrderStatus::REJECTED;
            LOG() << "[OrderBook:" << symbol_ << "] Order " << order.clOrdID 
                  << " remaining " << order.leavesQty 
                  << (partialFilled ? " canceled" : " rejected") 
                  << " (" << cancelReason << ")";
        }
    }

    return trades;
}

std::vector<Trade> OrderBook::matchBuyOrder(Order& buyOrder) {
    std::vector<Trade> trades;
    
    // 遍历卖盘（从最低价开始）
    auto it = asks_.begin();
    while (it != asks_.end() && buyOrder.leavesQty > 0) {
        PriceLevel& level = it->second;
        
        // 检查价格是否可以成交
        // 市价单：不检查价格，直接成交
        // 限价单：买价 >= 卖价 才能成交
        if (buyOrder.ordType == OrderType::LIMIT && buyOrder.price < level.price) {
            break;  // 后面的卖价更高，不可能成交
        }

        // 遍历该价位的订单
        auto orderIt = level.orders.begin();
        while (orderIt != level.orders.end() && buyOrder.leavesQty > 0) {
            Order& sellOrder = *orderIt;
            
            // 计算成交数量
            int64_t matchQty = std::min(buyOrder.leavesQty, sellOrder.leavesQty);
            double matchPrice = sellOrder.price;  // 成交价取被动方价格
            
            // 更新买单
            buyOrder.cumQty += matchQty;
            buyOrder.leavesQty -= matchQty;
            buyOrder.avgPx = (buyOrder.avgPx * (buyOrder.cumQty - matchQty) + matchPrice * matchQty) 
                           / buyOrder.cumQty;
            auto now = std::chrono::system_clock::now();
            buyOrder.updateTime = now;
            
            if (buyOrder.leavesQty == 0) {
                buyOrder.status = OrderStatus::FILLED;
            } else {
                buyOrder.status = OrderStatus::PARTIALLY_FILLED;
            }

            // 更新卖单
            sellOrder.cumQty += matchQty;
            sellOrder.leavesQty -= matchQty;
            sellOrder.avgPx = (sellOrder.avgPx * (sellOrder.cumQty - matchQty) + matchPrice * matchQty)
                            / sellOrder.cumQty;
            sellOrder.updateTime = now;
            
            if (sellOrder.leavesQty == 0) {
                sellOrder.status = OrderStatus::FILLED;
            } else {
                sellOrder.status = OrderStatus::PARTIALLY_FILLED;
            }

            // 创建成交记录（包含完整的双方订单快照）
            Trade trade;
            trade.tradeID = generateTradeID();
            trade.symbol = symbol_;
            trade.price = matchPrice;
            trade.qty = matchQty;
            trade.timestamp = now;
            
            // 买方信息
            trade.buyOrderID = buyOrder.orderID;
            trade.buyClOrdID = buyOrder.clOrdID;
            trade.buyOrderQty = buyOrder.orderQty;
            trade.buyPrice = buyOrder.price;
            trade.buyOrdType = buyOrder.ordType;
            trade.buyCumQty = buyOrder.cumQty;
            trade.buyLeavesQty = buyOrder.leavesQty;
            trade.buyAvgPx = buyOrder.avgPx;
            trade.buyStatus = buyOrder.status;
            
            // 卖方信息
            trade.sellOrderID = sellOrder.orderID;
            trade.sellClOrdID = sellOrder.clOrdID;
            trade.sellOrderQty = sellOrder.orderQty;
            trade.sellPrice = sellOrder.price;
            trade.sellOrdType = sellOrder.ordType;
            trade.sellCumQty = sellOrder.cumQty;
            trade.sellLeavesQty = sellOrder.leavesQty;
            trade.sellAvgPx = sellOrder.avgPx;
            trade.sellStatus = sellOrder.status;
            
            trades.push_back(trade);
            
            LOG() << "[OrderBook:" << symbol_ << "] Trade: " << trade.tradeID
                  << " " << matchQty << " @ " << matchPrice
                  << " (Buy:" << buyOrder.clOrdID << " Sell:" << sellOrder.clOrdID << ")";
            
            if (sellOrder.leavesQty == 0) {
                // 从索引中移除
                orderIndex_.erase(sellOrder.clOrdID);
                orderIt = level.orders.erase(orderIt);
                askOrderCount_--;
            } else {
                ++orderIt;
            }
            
            level.totalQty -= matchQty;
        }

        // 如果该价位已空，移除
        if (level.orders.empty()) {
            it = asks_.erase(it);
        } else {
            ++it;
        }
    }

    return trades;
}

std::vector<Trade> OrderBook::matchSellOrder(Order& sellOrder) {
    std::vector<Trade> trades;
    
    // 遍历买盘（从最高价开始）
    auto it = bids_.begin();
    while (it != bids_.end() && sellOrder.leavesQty > 0) {
        PriceLevel& level = it->second;
        
        // 检查价格是否可以成交
        // 市价单：不检查价格，直接成交
        // 限价单：卖价 <= 买价 才能成交
        if (sellOrder.ordType == OrderType::LIMIT && sellOrder.price > level.price) {
            break;  // 后面的买价更低，不可能成交
        }

        // 遍历该价位的订单
        auto orderIt = level.orders.begin();
        while (orderIt != level.orders.end() && sellOrder.leavesQty > 0) {
            Order& buyOrder = *orderIt;
            
            // 计算成交数量
            int64_t matchQty = std::min(sellOrder.leavesQty, buyOrder.leavesQty);
            double matchPrice = buyOrder.price;  // 成交价取被动方价格
            
            // 更新卖单
            sellOrder.cumQty += matchQty;
            sellOrder.leavesQty -= matchQty;
            sellOrder.avgPx = (sellOrder.avgPx * (sellOrder.cumQty - matchQty) + matchPrice * matchQty)
                            / sellOrder.cumQty;
            auto now = std::chrono::system_clock::now();
            sellOrder.updateTime = now;
            
            if (sellOrder.leavesQty == 0) {
                sellOrder.status = OrderStatus::FILLED;
            } else {
                sellOrder.status = OrderStatus::PARTIALLY_FILLED;
            }

            // 更新买单
            buyOrder.cumQty += matchQty;
            buyOrder.leavesQty -= matchQty;
            buyOrder.avgPx = (buyOrder.avgPx * (buyOrder.cumQty - matchQty) + matchPrice * matchQty)
                           / buyOrder.cumQty;
            buyOrder.updateTime = now;
            
            if (buyOrder.leavesQty == 0) {
                buyOrder.status = OrderStatus::FILLED;
            } else {
                buyOrder.status = OrderStatus::PARTIALLY_FILLED;
            }

            // 创建成交记录（包含完整的双方订单快照）
            Trade trade;
            trade.tradeID = generateTradeID();
            trade.symbol = symbol_;
            trade.price = matchPrice;
            trade.qty = matchQty;
            trade.timestamp = now;
            
            // 买方信息
            trade.buyOrderID = buyOrder.orderID;
            trade.buyClOrdID = buyOrder.clOrdID;
            trade.buyOrderQty = buyOrder.orderQty;
            trade.buyPrice = buyOrder.price;
            trade.buyOrdType = buyOrder.ordType;
            trade.buyCumQty = buyOrder.cumQty;
            trade.buyLeavesQty = buyOrder.leavesQty;
            trade.buyAvgPx = buyOrder.avgPx;
            trade.buyStatus = buyOrder.status;
            
            // 卖方信息
            trade.sellOrderID = sellOrder.orderID;
            trade.sellClOrdID = sellOrder.clOrdID;
            trade.sellOrderQty = sellOrder.orderQty;
            trade.sellPrice = sellOrder.price;
            trade.sellOrdType = sellOrder.ordType;
            trade.sellCumQty = sellOrder.cumQty;
            trade.sellLeavesQty = sellOrder.leavesQty;
            trade.sellAvgPx = sellOrder.avgPx;
            trade.sellStatus = sellOrder.status;
            
            trades.push_back(trade);
            
            LOG() << "[OrderBook:" << symbol_ << "] Trade: " << trade.tradeID
                  << " " << matchQty << " @ " << matchPrice
                  << " (Buy:" << buyOrder.clOrdID << " Sell:" << sellOrder.clOrdID << ")";
            
            if (buyOrder.leavesQty == 0) {
                // 从索引中移除
                orderIndex_.erase(buyOrder.clOrdID);
                orderIt = level.orders.erase(orderIt);
                bidOrderCount_--;
            } else {
                ++orderIt;
            }
            
            level.totalQty -= matchQty;
        }

        // 如果该价位已空，移除
        if (level.orders.empty()) {
            it = bids_.erase(it);
        } else {
            ++it;
        }
    }

    return trades;
}

void OrderBook::addToBids(const Order& order) {
    auto& level = bids_[order.price];
    if (level.orders.empty()) {
        level.price = order.price;
    }
    level.orders.push_back(order);
    level.totalQty += order.leavesQty;
    
    orderIndex_[order.clOrdID] = {OrderSide::BUY, order.price};
    bidOrderCount_++;
    
    LOG() << "[OrderBook:" << symbol_ << "] Order " << order.clOrdID 
          << " added to bids @ " << order.price;
}

void OrderBook::addToAsks(const Order& order) {
    auto& level = asks_[order.price];
    if (level.orders.empty()) {
        level.price = order.price;
    }
    level.orders.push_back(order);
    level.totalQty += order.leavesQty;
    
    orderIndex_[order.clOrdID] = {OrderSide::SELL, order.price};
    askOrderCount_++;
    
    LOG() << "[OrderBook:" << symbol_ << "] Order " << order.clOrdID 
          << " added to asks @ " << order.price;
}

std::optional<Order> OrderBook::cancelOrder(const std::string& clOrdID) {
    auto indexIt = orderIndex_.find(clOrdID);
    if (indexIt == orderIndex_.end()) {
        LOG() << "[OrderBook:" << symbol_ << "] Cancel failed: order " << clOrdID << " not found";
        return std::nullopt;
    }
    
    return removeOrder(clOrdID, indexIt->second.side);
}

std::optional<Order> OrderBook::removeOrder(const std::string& clOrdID, OrderSide side) {
    auto indexIt = orderIndex_.find(clOrdID);
    if (indexIt == orderIndex_.end()) {
        return std::nullopt;
    }
    
    double price = indexIt->second.price;
    
    if (side == OrderSide::BUY) {
        auto levelIt = bids_.find(price);
        if (levelIt != bids_.end()) {
            auto& orders = levelIt->second.orders;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->clOrdID == clOrdID) {
                    Order canceledOrder = *it;
                    canceledOrder.status = OrderStatus::CANCELED;
                    canceledOrder.updateTime = std::chrono::system_clock::now();
                    
                    levelIt->second.totalQty -= it->leavesQty;
                    orders.erase(it);
                    bidOrderCount_--;
                    
                    if (orders.empty()) {
                        bids_.erase(levelIt);
                    }
                    
                    orderIndex_.erase(clOrdID);
                    
                    LOG() << "[OrderBook:" << symbol_ << "] Order " << clOrdID << " canceled";
                    return canceledOrder;
                }
            }
        }
    } else {
        auto levelIt = asks_.find(price);
        if (levelIt != asks_.end()) {
            auto& orders = levelIt->second.orders;
            for (auto it = orders.begin(); it != orders.end(); ++it) {
                if (it->clOrdID == clOrdID) {
                    Order canceledOrder = *it;
                    canceledOrder.status = OrderStatus::CANCELED;
                    canceledOrder.updateTime = std::chrono::system_clock::now();
                    
                    levelIt->second.totalQty -= it->leavesQty;
                    orders.erase(it);
                    askOrderCount_--;
                    
                    if (orders.empty()) {
                        asks_.erase(levelIt);
                    }
                    
                    orderIndex_.erase(clOrdID);
                    
                    LOG() << "[OrderBook:" << symbol_ << "] Order " << clOrdID << " canceled";
                    return canceledOrder;
                }
            }
        }
    }
    
    return std::nullopt;
}

const Order* OrderBook::findOrder(const std::string& clOrdID) const {
    auto indexIt = orderIndex_.find(clOrdID);
    if (indexIt == orderIndex_.end()) {
        return nullptr;
    }
    
    double price = indexIt->second.price;
    OrderSide side = indexIt->second.side;
    
    if (side == OrderSide::BUY) {
        auto levelIt = bids_.find(price);
        if (levelIt != bids_.end()) {
            for (const auto& order : levelIt->second.orders) {
                if (order.clOrdID == clOrdID) {
                    return &order;
                }
            }
        }
    } else {
        auto levelIt = asks_.find(price);
        if (levelIt != asks_.end()) {
            for (const auto& order : levelIt->second.orders) {
                if (order.clOrdID == clOrdID) {
                    return &order;
                }
            }
        }
    }
    
    return nullptr;
}

std::optional<double> OrderBook::getBestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<double> OrderBook::getBestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::vector<PriceLevel> OrderBook::getBidLevels(size_t levels) const {
    std::vector<PriceLevel> result;
    result.reserve(levels);
    
    size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count >= levels) break;
        result.push_back(level);
        count++;
    }
    
    return result;
}

std::vector<PriceLevel> OrderBook::getAskLevels(size_t levels) const {
    std::vector<PriceLevel> result;
    result.reserve(levels);
    
    size_t count = 0;
    for (const auto& [price, level] : asks_) {
        if (count >= levels) break;
        result.push_back(level);
        count++;
    }
    
    return result;
}

int64_t OrderBook::calculateMatchableQty(const Order& order) const {
    int64_t matchableQty = 0;
    
    if (order.side == OrderSide::BUY) {
        // 买单：遍历卖盘计算可成交数量
        for (const auto& [askPrice, level] : asks_) {
            // 市价单不检查价格，限价单检查价格
            if (order.ordType == OrderType::LIMIT && order.price < askPrice) {
                break;  // 后面的卖价更高，不可能成交
            }
            matchableQty += level.totalQty;
            if (matchableQty >= order.orderQty) {
                return order.orderQty;  // 已经足够
            }
        }
    } else {
        // 卖单：遍历买盘计算可成交数量
        for (const auto& [bidPrice, level] : bids_) {
            // 市价单不检查价格，限价单检查价格
            if (order.ordType == OrderType::LIMIT && order.price > bidPrice) {
                break;  // 后面的买价更低，不可能成交
            }
            matchableQty += level.totalQty;
            if (matchableQty >= order.orderQty) {
                return order.orderQty;  // 已经足够
            }
        }
    }
    
    return matchableQty;
}

} // namespace fix40
