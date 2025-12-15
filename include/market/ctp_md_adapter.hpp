/**
 * @file ctp_md_adapter.hpp
 * @brief CTP 行情适配器
 *
 * 对接 CTP/SimNow 行情 API，将 CTP 行情数据转换为内部 MarketData 格式。
 * 仅在 macOS/Linux 平台且启用 CTP 时编译。
 */

#pragma once

#ifdef ENABLE_CTP

#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include "market/md_adapter.hpp"
#include "ThostFtdcMdApi.h"

namespace fix40 {

/**
 * @brief CTP 行情配置
 */
struct CtpMdConfig {
    std::string mdFront;      ///< 行情前置地址 tcp://ip:port
    std::string brokerId;     ///< 经纪商代码
    std::string userId;       ///< 用户ID
    std::string password;     ///< 密码
    std::string flowPath;     ///< 流文件路径
};

// 前向声明
class CtpMdAdapter;

/**
 * @brief CTP 行情 SPI 回调实现
 */
class CtpMdSpi final : public CThostFtdcMdSpi {
public:
    explicit CtpMdSpi(CtpMdAdapter* adapter) : adapter_(adapter) {}
    ~CtpMdSpi() = default;

    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                        CThostFtdcRspInfoField* pRspInfo,
                        int nRequestID, bool bIsLast) override;
    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                            CThostFtdcRspInfoField* pRspInfo,
                            int nRequestID, bool bIsLast) override;
    void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                              CThostFtdcRspInfoField* pRspInfo,
                              int nRequestID, bool bIsLast) override;
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) override;
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnHeartBeatWarning(int nTimeLapse) override;

private:
    CtpMdAdapter* adapter_;
};

/**
 * @class CtpMdAdapter
 * @brief CTP 行情适配器
 *
 * 实现 MdAdapter 接口，对接 CTP/SimNow 行情源。
 */
class CtpMdAdapter : public MdAdapter {
    friend class CtpMdSpi;

public:
    /**
     * @brief 构造函数
     * @param queue 行情数据输出队列
     * @param config CTP 配置
     */
    CtpMdAdapter(moodycamel::BlockingConcurrentQueue<MarketData>& queue,
                 const CtpMdConfig& config);

    ~CtpMdAdapter() override;

    // 禁止拷贝
    CtpMdAdapter(const CtpMdAdapter&) = delete;
    CtpMdAdapter& operator=(const CtpMdAdapter&) = delete;

    // MdAdapter 接口实现
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    MdAdapterState getState() const override { return state_.load(); }

    bool subscribe(const std::vector<std::string>& instruments) override;
    bool unsubscribe(const std::vector<std::string>& instruments) override;

    void setStateCallback(StateCallback callback) override;

    std::string getName() const override { return "CTP"; }
    std::string getTradingDay() const override;

private:
    /**
     * @brief 发送登录请求
     */
    void doLogin();

    /**
     * @brief 订阅待订阅列表中的合约
     */
    void doSubscribePending();

    /**
     * @brief 通知状态变更
     */
    void notifyState(MdAdapterState state, const std::string& message);

    /**
     * @brief 将 CTP 行情转换为内部格式
     */
    static MarketData convertMarketData(const CThostFtdcDepthMarketDataField* pData);

    CtpMdConfig config_;
    CThostFtdcMdApi* api_ = nullptr;
    std::unique_ptr<CtpMdSpi> spi_;

    std::atomic<bool> running_{false};
    std::atomic<MdAdapterState> state_{MdAdapterState::DISCONNECTED};
    std::atomic<int> requestId_{0};

    mutable std::mutex mutex_;
    std::set<std::string> subscribedInstruments_;
    std::set<std::string> pendingSubscribe_;
    std::string tradingDay_;

    StateCallback stateCallback_;
};

/**
 * @brief 从配置文件加载 CTP 配置
 * @param filename 配置文件路径
 * @return CTP 配置
 */
CtpMdConfig loadCtpConfig(const std::string& filename);

} // namespace fix40

#endif // ENABLE_CTP
