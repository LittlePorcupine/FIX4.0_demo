/**
 * @file mock_md_adapter.hpp
 * @brief 模拟行情适配器
 *
 * 用于测试和开发的模拟行情源，生成随机行情数据。
 */

#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <map>
#include <random>
#include <chrono>
#include "market/md_adapter.hpp"

namespace fix40 {

/**
 * @class MockMdAdapter
 * @brief 模拟行情适配器
 *
 * 生成模拟行情数据用于测试。特点：
 * - 在独立线程中按固定频率生成行情
 * - 支持订阅/退订合约
 * - 价格在基准价附近随机波动
 *
 * @par 使用示例
 * @code
 * moodycamel::BlockingConcurrentQueue<MarketData> queue;
 * MockMdAdapter adapter(queue);
 * 
 * adapter.setTickInterval(std::chrono::milliseconds(500));  // 500ms 一个 tick
 * adapter.start();
 * adapter.subscribe({"IF2401", "IC2401"});
 * 
 * // 消费行情...
 * 
 * adapter.stop();
 * @endcode
 */
class MockMdAdapter : public MdAdapter {
public:
    /**
     * @brief 构造模拟行情适配器
     * @param queue 行情数据输出队列
     */
    explicit MockMdAdapter(moodycamel::BlockingConcurrentQueue<MarketData>& queue);

    /**
     * @brief 析构函数
     */
    ~MockMdAdapter() override;

    // 禁止拷贝
    MockMdAdapter(const MockMdAdapter&) = delete;
    MockMdAdapter& operator=(const MockMdAdapter&) = delete;

    // =========================================================================
    // MdAdapter 接口实现
    // =========================================================================

    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    MdAdapterState getState() const override { return state_.load(); }

    bool subscribe(const std::vector<std::string>& instruments) override;
    bool unsubscribe(const std::vector<std::string>& instruments) override;

    void setStateCallback(StateCallback callback) override;

    std::string getName() const override { return "Mock"; }
    std::string getTradingDay() const override;

    // =========================================================================
    // Mock 特有配置
    // =========================================================================

    /**
     * @brief 设置行情生成间隔
     * @param interval 间隔时间
     * @note 应在 start() 之前调用，运行时修改行为未定义
     */
    void setTickInterval(std::chrono::milliseconds interval) {
        tickIntervalMs_.store(interval.count());
    }

    /**
     * @brief 设置基准价格
     * @param instrument 合约代码
     * @param basePrice 基准价格
     *
     * 模拟行情会在基准价格附近波动。
     * 未设置的合约使用默认基准价 5000.0。
     */
    void setBasePrice(const std::string& instrument, double basePrice);

    /**
     * @brief 设置价格波动幅度（百分比）
     * @param volatility 波动幅度，如 0.01 表示 1%
     * @note 应在 start() 之前调用，运行时修改行为未定义
     */
    void setVolatility(double volatility) {
        volatility_.store(volatility);
    }

private:
    /**
     * @brief 行情生成线程主循环
     */
    void run();

    /**
     * @brief 生成单个合约的行情
     * @param instrument 合约代码
     * @return MarketData 生成的行情数据
     */
    MarketData generateTick(const std::string& instrument);

    /**
     * @brief 通知状态变更
     * @param state 新状态
     * @param message 描述信息
     */
    void notifyState(MdAdapterState state, const std::string& message);

    /**
     * @brief 获取当前时间字符串
     * @return std::string 格式 HH:MM:SS
     */
    std::string getCurrentTime() const;

    std::atomic<bool> running_{false};
    std::atomic<MdAdapterState> state_{MdAdapterState::DISCONNECTED};
    std::thread workerThread_;

    mutable std::mutex mutex_;
    std::set<std::string> subscribedInstruments_;
    std::map<std::string, double> basePrices_;
    std::map<std::string, double> lastPrices_;

    StateCallback stateCallback_;
    std::atomic<int64_t> tickIntervalMs_{1000};  ///< 行情间隔（毫秒），使用 int64_t 保证 lock-free
    std::atomic<double> volatility_{0.005};      ///< 默认 0.5% 波动

    std::mt19937 rng_;                       ///< 随机数生成器（仅工作线程访问）
    const std::string tradingDay_;           ///< 交易日（构造后不变）
};

} // namespace fix40
