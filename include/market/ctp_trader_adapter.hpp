/**
 * @file ctp_trader_adapter.hpp
 * @brief CTP 交易适配器
 *
 * 对接 CTP/SimNow 交易 API，用于查询合约信息。
 * 本适配器仅用于查询功能，不进行实际交易。
 * 仅在 macOS/Linux 平台且启用 CTP 时编译。
 */

#pragma once

#ifdef ENABLE_CTP

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <functional>
#include "ThostFtdcTraderApi.h"

namespace fix40 {

// 前向声明
class InstrumentManager;
class CtpTraderAdapter;

/**
 * @brief CTP 交易适配器状态
 */
enum class CtpTraderState {
    DISCONNECTED,   ///< 未连接
    CONNECTING,     ///< 连接中
    CONNECTED,      ///< 已连接（未登录）
    AUTHENTICATING, ///< 认证中
    LOGGING_IN,     ///< 登录中
    READY,          ///< 就绪（已登录）
    QUERYING,       ///< 查询中
    ERROR           ///< 错误状态
};

/**
 * @brief CTP 交易配置
 */
struct CtpTraderConfig {
    std::string traderFront;  ///< 交易前置地址 tcp://ip:port
    std::string brokerId;     ///< 经纪商代码
    std::string userId;       ///< 用户ID
    std::string password;     ///< 密码
    std::string appId;        ///< AppID（穿透式监管）
    std::string authCode;     ///< 授权码（穿透式监管）
    std::string flowPath;     ///< 流文件路径
};

/**
 * @brief CTP 交易 SPI 回调实现
 */
class CtpTraderSpi final : public CThostFtdcTraderSpi {
public:
    explicit CtpTraderSpi(CtpTraderAdapter* adapter) : adapter_(adapter) {}
    ~CtpTraderSpi() = default;

    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField,
                           CThostFtdcRspInfoField* pRspInfo,
                           int nRequestID, bool bIsLast) override;
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                        CThostFtdcRspInfoField* pRspInfo,
                        int nRequestID, bool bIsLast) override;
    void OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument,
                            CThostFtdcRspInfoField* pRspInfo,
                            int nRequestID, bool bIsLast) override;
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;
    void OnHeartBeatWarning(int nTimeLapse) override;

private:
    CtpTraderAdapter* adapter_;
};

/**
 * @class CtpTraderAdapter
 * @brief CTP 交易适配器
 *
 * 实现 CTP 交易 API 的封装，主要用于查询合约信息。
 * 
 * @par 使用流程
 * 1. 创建适配器，传入配置
 * 2. 调用 start() 连接并登录
 * 3. 等待 onReady 回调或调用 waitForReady()
 * 4. 调用 queryInstruments() 查询合约
 * 5. 调用 stop() 停止
 *
 * @par 使用示例
 * @code
 * CtpTraderConfig config;
 * config.traderFront = "tcp://180.168.146.187:10201";
 * config.brokerId = "9999";
 * config.userId = "your_user";
 * config.password = "your_pass";
 * 
 * CtpTraderAdapter adapter(config);
 * adapter.setInstrumentManager(&instrumentMgr);
 * adapter.start();
 * adapter.waitForReady(10);  // 等待最多10秒
 * adapter.queryInstruments();
 * adapter.waitForQueryComplete(30);  // 等待查询完成
 * adapter.stop();
 * @endcode
 */
class CtpTraderAdapter {
    friend class CtpTraderSpi;

public:
    /// 状态变更回调
    using StateCallback = std::function<void(CtpTraderState state, const std::string& message)>;
    
    /// 查询完成回调
    using QueryCompleteCallback = std::function<void(int count)>;

    /**
     * @brief 构造函数
     * @param config CTP 配置
     */
    explicit CtpTraderAdapter(const CtpTraderConfig& config);

    ~CtpTraderAdapter();

    // 禁止拷贝
    CtpTraderAdapter(const CtpTraderAdapter&) = delete;
    CtpTraderAdapter& operator=(const CtpTraderAdapter&) = delete;

    /**
     * @brief 启动适配器
     * @return 启动成功返回 true
     */
    bool start();

    /**
     * @brief 停止适配器
     */
    void stop();

    /**
     * @brief 检查是否正在运行
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 获取当前状态
     */
    CtpTraderState getState() const { return state_.load(); }

    /**
     * @brief 等待就绪状态
     * @param timeoutSeconds 超时秒数
     * @return 就绪返回 true，超时返回 false
     */
    bool waitForReady(int timeoutSeconds);

    /**
     * @brief 查询所有合约
     * @return 请求发送成功返回 true
     */
    bool queryInstruments();

    /**
     * @brief 查询指定交易所的合约
     * @param exchangeId 交易所代码（如 "CFFEX", "SHFE"）
     * @return 请求发送成功返回 true
     */
    bool queryInstruments(const std::string& exchangeId);

    /**
     * @brief 等待查询完成
     * @param timeoutSeconds 超时秒数
     * @return 完成返回 true，超时返回 false
     */
    bool waitForQueryComplete(int timeoutSeconds);

    /**
     * @brief 设置合约管理器
     * @param manager 合约管理器指针
     */
    void setInstrumentManager(InstrumentManager* manager) { instrumentManager_ = manager; }

    /**
     * @brief 设置状态回调
     */
    void setStateCallback(StateCallback callback) { stateCallback_ = std::move(callback); }

    /**
     * @brief 设置查询完成回调
     */
    void setQueryCompleteCallback(QueryCompleteCallback callback) { queryCompleteCallback_ = std::move(callback); }

    /**
     * @brief 获取交易日
     */
    std::string getTradingDay() const;

    /**
     * @brief 获取查询到的合约数量
     */
    int getQueriedInstrumentCount() const { return queriedCount_.load(); }

private:
    /**
     * @brief 发送认证请求
     */
    void doAuthenticate();

    /**
     * @brief 发送登录请求
     */
    void doLogin();

    /**
     * @brief 通知状态变更
     */
    void notifyState(CtpTraderState state, const std::string& message);

    CtpTraderConfig config_;
    CThostFtdcTraderApi* api_ = nullptr;
    std::unique_ptr<CtpTraderSpi> spi_;

    std::atomic<bool> running_{false};
    std::atomic<CtpTraderState> state_{CtpTraderState::DISCONNECTED};
    std::atomic<int> requestId_{0};
    std::atomic<int> queriedCount_{0};
    std::atomic<bool> queryComplete_{false};

    mutable std::mutex mutex_;
    std::condition_variable readyCv_;
    std::condition_variable queryCv_;
    std::string tradingDay_;

    InstrumentManager* instrumentManager_ = nullptr;
    StateCallback stateCallback_;
    QueryCompleteCallback queryCompleteCallback_;
};

/**
 * @brief 从配置文件加载 CTP 交易配置
 * @param filename 配置文件路径
 * @return CTP 配置
 */
CtpTraderConfig loadCtpTraderConfig(const std::string& filename);

} // namespace fix40

#endif // ENABLE_CTP
