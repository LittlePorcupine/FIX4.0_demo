/**
 * @file md_adapter.hpp
 * @brief 行情适配器接口定义
 *
 * 定义行情数据源的抽象接口，支持多种行情源（CTP、模拟等）。
 * 适配器负责：连接数据源 -> 接收原始数据 -> 转换为 MarketData -> 写入队列
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "market/market_data.hpp"
#include "base/blockingconcurrentqueue.h"

namespace fix40 {

/**
 * @enum MdAdapterState
 * @brief 行情适配器状态
 */
enum class MdAdapterState {
    DISCONNECTED,   ///< 未连接
    CONNECTING,     ///< 连接中
    CONNECTED,      ///< 已连接（未登录）
    LOGGING_IN,     ///< 登录中
    READY,          ///< 就绪（已登录，可订阅）
    ERROR           ///< 错误状态
};

/**
 * @brief 状态变更回调类型
 * @param state 新状态
 * @param message 状态描述信息
 */
using StateCallback = std::function<void(MdAdapterState state, const std::string& message)>;

/**
 * @class MdAdapter
 * @brief 行情适配器抽象接口
 *
 * 所有行情数据源都应实现此接口。适配器的职责：
 * 1. 管理与数据源的连接
 * 2. 处理登录/登出
 * 3. 订阅/退订合约
 * 4. 将收到的行情转换为 MarketData 格式
 * 5. 将 MarketData 写入无锁队列供下游消费
 *
 * @par 线程模型
 * - 适配器内部可能有自己的回调线程（如 CTP）
 * - 所有行情数据通过无锁队列传递，避免阻塞回调线程
 * - start()/stop() 应由主线程调用
 *
 * @par 使用示例
 * @code
 * moodycamel::BlockingConcurrentQueue<MarketData> mdQueue;
 * auto adapter = std::make_unique<MockMdAdapter>(mdQueue);
 * 
 * adapter->setStateCallback([](MdAdapterState state, const std::string& msg) {
 *     LOG() << "State: " << static_cast<int>(state) << " - " << msg;
 * });
 * 
 * adapter->start();
 * adapter->subscribe({"IF2401", "IC2401"});
 * 
 * // 消费行情
 * MarketData md;
 * while (mdQueue.wait_dequeue_timed(md, std::chrono::milliseconds(100))) {
 *     process(md);
 * }
 * 
 * adapter->stop();
 * @endcode
 */
class MdAdapter {
public:
    /**
     * @brief 虚析构函数
     */
    virtual ~MdAdapter() = default;

    // =========================================================================
    // 生命周期管理
    // =========================================================================

    /**
     * @brief 启动适配器
     * @return true 启动成功
     * @return false 启动失败
     *
     * 启动后适配器会尝试连接数据源并登录。
     * 状态变化通过 StateCallback 通知。
     */
    virtual bool start() = 0;

    /**
     * @brief 停止适配器
     *
     * 断开与数据源的连接，释放资源。
     * 调用后适配器进入 DISCONNECTED 状态。
     */
    virtual void stop() = 0;

    /**
     * @brief 检查适配器是否正在运行
     * @return true 正在运行
     * @return false 已停止
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief 获取当前状态
     * @return MdAdapterState 当前状态
     */
    virtual MdAdapterState getState() const = 0;

    // =========================================================================
    // 订阅管理
    // =========================================================================

    /**
     * @brief 订阅合约行情
     * @param instruments 合约代码列表
     * @return true 订阅请求已发送
     * @return false 订阅失败（如未连接）
     *
     * @note 订阅结果通过 StateCallback 异步通知
     */
    virtual bool subscribe(const std::vector<std::string>& instruments) = 0;

    /**
     * @brief 退订合约行情
     * @param instruments 合约代码列表
     * @return true 退订请求已发送
     * @return false 退订失败
     */
    virtual bool unsubscribe(const std::vector<std::string>& instruments) = 0;

    // =========================================================================
    // 回调设置
    // =========================================================================

    /**
     * @brief 设置状态变更回调
     * @param callback 回调函数
     */
    virtual void setStateCallback(StateCallback callback) = 0;

    // =========================================================================
    // 信息查询
    // =========================================================================

    /**
     * @brief 获取适配器名称
     * @return std::string 适配器名称（如 "CTP", "Mock"）
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 获取交易日
     * @return std::string 交易日 (YYYYMMDD)，未连接时返回空
     */
    virtual std::string getTradingDay() const = 0;

protected:
    /**
     * @brief 构造函数
     * @param queue 行情数据输出队列
     */
    explicit MdAdapter(moodycamel::BlockingConcurrentQueue<MarketData>& queue)
        : marketDataQueue_(queue) {}

    /**
     * @brief 将行情数据写入队列
     * @param data 行情数据
     *
     * 子类在收到行情后调用此方法将数据写入队列。
     */
    void pushMarketData(const MarketData& data) {
        marketDataQueue_.enqueue(data);
    }

    /**
     * @brief 将行情数据写入队列（移动语义）
     * @param data 行情数据
     */
    void pushMarketData(MarketData&& data) {
        marketDataQueue_.enqueue(std::move(data));
    }

    /// 行情数据输出队列
    moodycamel::BlockingConcurrentQueue<MarketData>& marketDataQueue_;
};

} // namespace fix40
