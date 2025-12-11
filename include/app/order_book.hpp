/**
 * @file order_book.hpp
 * @brief 订单簿实现
 *
 * 管理单个合约的买卖盘，实现价格优先、时间优先的撮合逻辑。
 */

#pragma once

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include "app/order.hpp"

namespace fix40 {

/**
 * @struct Trade
 * @brief 成交记录
 */
struct Trade {
    std::string tradeID;           ///< 成交ID
    std::string buyOrderID;        ///< 买方订单ID
    std::string sellOrderID;       ///< 卖方订单ID
    std::string buyClOrdID;        ///< 买方客户订单ID
    std::string sellClOrdID;       ///< 卖方客户订单ID
    std::string symbol;            ///< 合约代码
    double price;                  ///< 成交价格
    int64_t qty;                   ///< 成交数量
    std::chrono::system_clock::time_point timestamp;  ///< 成交时间

    Trade() : price(0.0), qty(0) {}
};

/**
 * @struct PriceLevel
 * @brief 价格档位
 *
 * 同一价格的所有订单，按时间顺序排列。
 */
struct PriceLevel {
    double price;                  ///< 价格
    std::list<Order> orders;       ///< 订单队列（时间优先）
    int64_t totalQty;              ///< 该价位总数量

    explicit PriceLevel(double p = 0.0) : price(p), totalQty(0) {}

    bool empty() const { return orders.empty(); }
};

/**
 * @class OrderBook
 * @brief 订单簿
 *
 * 管理单个合约的买卖盘，实现撮合逻辑。
 *
 * @par 撮合规则
 * - 价格优先：买盘价高者优先，卖盘价低者优先
 * - 时间优先：同价位先到者优先成交
 * - 成交价格：取被动方（挂单方）价格
 *
 * @par 线程安全
 * OrderBook 本身不是线程安全的，应由 MatchingEngine 在单线程中调用。
 *
 * @par 使用示例
 * @code
 * OrderBook book("IF2401");
 * 
 * Order buyOrder;
 * buyOrder.side = OrderSide::BUY;
 * buyOrder.price = 5000.0;
 * buyOrder.orderQty = 10;
 * 
 * auto trades = book.addOrder(buyOrder);
 * for (const auto& trade : trades) {
 *     // 处理成交
 * }
 * @endcode
 */
class OrderBook {
public:
    /**
     * @brief 构造订单簿
     * @param symbol 合约代码
     */
    explicit OrderBook(const std::string& symbol);

    /**
     * @brief 获取合约代码
     */
    const std::string& getSymbol() const { return symbol_; }

    // =========================================================================
    // 订单操作
    // =========================================================================

    /**
     * @brief 添加订单并尝试撮合
     * @param order 订单（会被修改：设置 orderID、更新状态）
     * @return std::vector<Trade> 成交记录列表
     *
     * 流程：
     * 1. 生成 OrderID
     * 2. 尝试与对手盘撮合
     * 3. 未成交部分挂入订单簿
     * 4. 返回成交记录
     */
    std::vector<Trade> addOrder(Order& order);

    /**
     * @brief 撤销订单
     * @param clOrdID 客户订单ID
     * @return std::optional<Order> 被撤销的订单，不存在返回 nullopt
     */
    std::optional<Order> cancelOrder(const std::string& clOrdID);

    /**
     * @brief 查找订单
     * @param clOrdID 客户订单ID
     * @return const Order* 订单指针，不存在返回 nullptr
     */
    const Order* findOrder(const std::string& clOrdID) const;

    // =========================================================================
    // 行情查询
    // =========================================================================

    /**
     * @brief 获取最优买价
     * @return std::optional<double> 最高买价，无买盘返回 nullopt
     */
    std::optional<double> getBestBid() const;

    /**
     * @brief 获取最优卖价
     * @return std::optional<double> 最低卖价，无卖盘返回 nullopt
     */
    std::optional<double> getBestAsk() const;

    /**
     * @brief 获取买盘深度
     * @param levels 档位数量
     * @return std::vector<PriceLevel> 买盘各档位（价格降序）
     */
    std::vector<PriceLevel> getBidLevels(size_t levels = 5) const;

    /**
     * @brief 获取卖盘深度
     * @param levels 档位数量
     * @return std::vector<PriceLevel> 卖盘各档位（价格升序）
     */
    std::vector<PriceLevel> getAskLevels(size_t levels = 5) const;

    /**
     * @brief 获取买盘总订单数
     */
    size_t getBidOrderCount() const { return bidOrderCount_; }

    /**
     * @brief 获取卖盘总订单数
     */
    size_t getAskOrderCount() const { return askOrderCount_; }

    /**
     * @brief 检查订单簿是否为空
     */
    bool empty() const { return bids_.empty() && asks_.empty(); }

private:
    /**
     * @brief 生成订单ID
     */
    std::string generateOrderID();

    /**
     * @brief 生成成交ID
     */
    std::string generateTradeID();

    /**
     * @brief 撮合买单
     * @param order 买单
     * @return std::vector<Trade> 成交记录
     */
    std::vector<Trade> matchBuyOrder(Order& order);

    /**
     * @brief 撮合卖单
     * @param order 卖单
     * @return std::vector<Trade> 成交记录
     */
    std::vector<Trade> matchSellOrder(Order& order);

    /**
     * @brief 将订单挂入买盘
     */
    void addToBids(const Order& order);

    /**
     * @brief 将订单挂入卖盘
     */
    void addToAsks(const Order& order);

    /**
     * @brief 从订单簿移除订单
     * @param clOrdID 客户订单ID
     * @param side 买卖方向
     * @return std::optional<Order> 被移除的订单
     */
    std::optional<Order> removeOrder(const std::string& clOrdID, OrderSide side);

    std::string symbol_;  ///< 合约代码

    /// 买盘：价格降序（greater 使 map 按价格从高到低排列）
    std::map<double, PriceLevel, std::greater<double>> bids_;
    
    /// 卖盘：价格升序（less 使 map 按价格从低到高排列）
    std::map<double, PriceLevel, std::less<double>> asks_;

    /// 订单索引：ClOrdID -> (Side, Price) 用于快速查找
    struct OrderLocation {
        OrderSide side;
        double price;
    };
    std::unordered_map<std::string, OrderLocation> orderIndex_;

    size_t bidOrderCount_ = 0;   ///< 买盘订单数
    size_t askOrderCount_ = 0;   ///< 卖盘订单数
    uint64_t nextOrderID_ = 1;   ///< 订单ID计数器
    uint64_t nextTradeID_ = 1;   ///< 成交ID计数器
};

} // namespace fix40
