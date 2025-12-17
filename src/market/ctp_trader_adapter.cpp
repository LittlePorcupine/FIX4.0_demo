/**
 * @file ctp_trader_adapter.cpp
 * @brief CTP 交易适配器实现
 */

#ifdef ENABLE_CTP

#include "market/ctp_trader_adapter.hpp"
#include "app/manager/instrument_manager.hpp"
#include "app/model/instrument.hpp"
#include "base/logger.hpp"
#include <fstream>
#include <cstring>
#include <filesystem>

namespace fix40 {

// =============================================================================
// CtpTraderSpi 实现
// =============================================================================

void CtpTraderSpi::OnFrontConnected() {
    LOG() << "[CTP Trader] 前置连接成功";
    adapter_->notifyState(CtpTraderState::CONNECTED, "前置连接成功");
    
    // 如果配置了 AppID，先进行认证
    if (!adapter_->config_.appId.empty()) {
        adapter_->doAuthenticate();
    } else {
        adapter_->doLogin();
    }
}

void CtpTraderSpi::OnFrontDisconnected(int nReason) {
    LOG() << "[CTP Trader] 连接断开, 原因: " << nReason;
    adapter_->notifyState(CtpTraderState::DISCONNECTED, 
                          "连接断开, 原因: " + std::to_string(nReason));
}

void CtpTraderSpi::OnRspAuthenticate(CThostFtdcRspAuthenticateField* pRspAuthenticateField,
                                      CThostFtdcRspInfoField* pRspInfo,
                                      int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP Trader] 认证失败, 错误码: " << pRspInfo->ErrorID 
              << ", 错误信息: " << pRspInfo->ErrorMsg;
        adapter_->notifyState(CtpTraderState::ERROR, 
                              std::string("认证失败: ") + pRspInfo->ErrorMsg);
        return;
    }

    LOG() << "[CTP Trader] 认证成功";
    adapter_->doLogin();
}

void CtpTraderSpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                                   CThostFtdcRspInfoField* pRspInfo,
                                   int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP Trader] 登录失败, 错误码: " << pRspInfo->ErrorID 
              << ", 错误信息: " << pRspInfo->ErrorMsg;
        adapter_->notifyState(CtpTraderState::ERROR, 
                              std::string("登录失败: ") + pRspInfo->ErrorMsg);
        return;
    }

    LOG() << "[CTP Trader] 登录成功";
    if (pRspUserLogin) {
        std::lock_guard<std::mutex> lock(adapter_->mutex_);
        adapter_->tradingDay_ = pRspUserLogin->TradingDay;
        LOG() << "[CTP Trader] 交易日: " << adapter_->tradingDay_;
    }

    adapter_->notifyState(CtpTraderState::READY, "登录成功");
}

void CtpTraderSpi::OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument,
                                       CThostFtdcRspInfoField* pRspInfo,
                                       int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP Trader] 查询合约失败, 错误码: " << pRspInfo->ErrorID
              << ", 错误信息: " << pRspInfo->ErrorMsg;
        return;
    }

    if (pInstrument && adapter_->instrumentManager_) {
        // 转换为内部 Instrument 结构
        Instrument inst;
        inst.instrumentId = pInstrument->InstrumentID;
        inst.exchangeId = pInstrument->ExchangeID;
        inst.productId = pInstrument->ProductID;
        inst.priceTick = pInstrument->PriceTick;
        inst.volumeMultiple = pInstrument->VolumeMultiple;
        
        // CTP 返回的保证金率可能为 0，需要设置默认值
        // 实际保证金率需要通过 ReqQryInstrumentMarginRate 查询
        // 这里使用一个合理的默认值
        inst.marginRate = 0.10;  // 默认 10%
        
        // 添加到管理器
        adapter_->instrumentManager_->addInstrument(inst);
        adapter_->queriedCount_++;
    }

    if (bIsLast) {
        LOG() << "[CTP Trader] 合约查询完成, 共 " << adapter_->queriedCount_.load() << " 个合约";
        adapter_->queryComplete_ = true;
        adapter_->queryCv_.notify_all();
        
        if (adapter_->queryCompleteCallback_) {
            adapter_->queryCompleteCallback_(adapter_->queriedCount_.load());
        }
    }
}

void CtpTraderSpi::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    if (pRspInfo) {
        LOG() << "[CTP Trader] 错误, 错误码: " << pRspInfo->ErrorID
              << ", 错误信息: " << pRspInfo->ErrorMsg;
    }
}

void CtpTraderSpi::OnHeartBeatWarning(int nTimeLapse) {
    LOG() << "[CTP Trader] 心跳超时警告, 距上次: " << nTimeLapse << "秒";
}

// =============================================================================
// CtpTraderAdapter 实现
// =============================================================================

CtpTraderAdapter::CtpTraderAdapter(const CtpTraderConfig& config)
    : config_(config) {
}

CtpTraderAdapter::~CtpTraderAdapter() {
    stop();
}

bool CtpTraderAdapter::start() {
    if (running_.load()) {
        return true;
    }

    LOG() << "[CTP Trader] 启动交易适配器";
    LOG() << "[CTP Trader] 前置地址: " << config_.traderFront;
    LOG() << "[CTP Trader] BrokerID: " << config_.brokerId;

    // 创建流文件目录
    if (!config_.flowPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(config_.flowPath, ec);
        if (ec) {
            LOG() << "[CTP Trader] 创建流文件目录失败: " << ec.message();
        }
    }

    // 创建 API
    api_ = CThostFtdcTraderApi::CreateFtdcTraderApi(config_.flowPath.c_str());
    if (!api_) {
        LOG() << "[CTP Trader] 创建 TraderApi 失败";
        return false;
    }

    // 创建 SPI
    spi_ = std::make_unique<CtpTraderSpi>(this);
    api_->RegisterSpi(spi_.get());

    // 订阅私有流和公共流（从头开始）
    api_->SubscribePrivateTopic(THOST_TERT_QUICK);
    api_->SubscribePublicTopic(THOST_TERT_QUICK);

    // 注册前置
    api_->RegisterFront(const_cast<char*>(config_.traderFront.c_str()));

    // 初始化
    running_ = true;
    state_ = CtpTraderState::CONNECTING;
    api_->Init();

    LOG() << "[CTP Trader] API 初始化完成";
    return true;
}

void CtpTraderAdapter::stop() {
    if (!running_.load()) {
        return;
    }

    LOG() << "[CTP Trader] 停止交易适配器";
    running_ = false;

    if (api_) {
        api_->Release();
        api_ = nullptr;
    }

    spi_.reset();
    state_ = CtpTraderState::DISCONNECTED;

    LOG() << "[CTP Trader] 交易适配器已停止";
}

bool CtpTraderAdapter::waitForReady(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(mutex_);
    return readyCv_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this] {
        return state_.load() == CtpTraderState::READY || 
               state_.load() == CtpTraderState::ERROR ||
               !running_.load();
    }) && state_.load() == CtpTraderState::READY;
}

bool CtpTraderAdapter::queryInstruments() {
    return queryInstruments("");
}

bool CtpTraderAdapter::queryInstruments(const std::string& exchangeId) {
    if (state_.load() != CtpTraderState::READY) {
        LOG() << "[CTP Trader] 未就绪，无法查询合约";
        return false;
    }

    CThostFtdcQryInstrumentField req{};
    if (!exchangeId.empty()) {
        std::strncpy(req.ExchangeID, exchangeId.c_str(), sizeof(req.ExchangeID) - 1);
    }

    queriedCount_ = 0;
    queryComplete_ = false;
    state_ = CtpTraderState::QUERYING;

    int ret = api_->ReqQryInstrument(&req, ++requestId_);
    LOG() << "[CTP Trader] 发送查询合约请求, 返回: " << ret;
    return ret == 0;
}

bool CtpTraderAdapter::waitForQueryComplete(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(mutex_);
    return queryCv_.wait_for(lock, std::chrono::seconds(timeoutSeconds), [this] {
        return queryComplete_.load() || !running_.load();
    }) && queryComplete_.load();
}

std::string CtpTraderAdapter::getTradingDay() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tradingDay_;
}

void CtpTraderAdapter::doAuthenticate() {
    CThostFtdcReqAuthenticateField req{};
    std::strncpy(req.BrokerID, config_.brokerId.c_str(), sizeof(req.BrokerID) - 1);
    std::strncpy(req.UserID, config_.userId.c_str(), sizeof(req.UserID) - 1);
    std::strncpy(req.AppID, config_.appId.c_str(), sizeof(req.AppID) - 1);
    std::strncpy(req.AuthCode, config_.authCode.c_str(), sizeof(req.AuthCode) - 1);

    state_ = CtpTraderState::AUTHENTICATING;
    int ret = api_->ReqAuthenticate(&req, ++requestId_);
    LOG() << "[CTP Trader] 发送认证请求, 返回: " << ret;
}

void CtpTraderAdapter::doLogin() {
    CThostFtdcReqUserLoginField req{};
    std::strncpy(req.BrokerID, config_.brokerId.c_str(), sizeof(req.BrokerID) - 1);
    std::strncpy(req.UserID, config_.userId.c_str(), sizeof(req.UserID) - 1);
    std::strncpy(req.Password, config_.password.c_str(), sizeof(req.Password) - 1);

    state_ = CtpTraderState::LOGGING_IN;
    
    // CTP 6.6.1+ 需要传入系统信息
    // 这里简化处理，传入空的系统信息
    int ret = api_->ReqUserLogin(&req, ++requestId_, 0, nullptr);
    LOG() << "[CTP Trader] 发送登录请求, 返回: " << ret;
}

void CtpTraderAdapter::notifyState(CtpTraderState state, const std::string& message) {
    state_ = state;
    
    if (state == CtpTraderState::READY || state == CtpTraderState::ERROR) {
        readyCv_.notify_all();
    }
    
    if (stateCallback_) {
        stateCallback_(state, message);
    }
}

// =============================================================================
// 配置加载
// =============================================================================

CtpTraderConfig loadCtpTraderConfig(const std::string& filename) {
    CtpTraderConfig config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG() << "[CTP Trader] 无法打开配置文件: " << filename;
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // 去除空格
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        if (key == "trader_front") config.traderFront = value;
        else if (key == "broker_id") config.brokerId = value;
        else if (key == "user_id") config.userId = value;
        else if (key == "password") config.password = value;
        else if (key == "app_id") config.appId = value;
        else if (key == "auth_code") config.authCode = value;
        else if (key == "trader_flow_path") config.flowPath = value;
    }

    // 默认流文件路径
    if (config.flowPath.empty()) {
        config.flowPath = "./ctp_trader_flow/";
    }

    return config;
}

} // namespace fix40

#endif // ENABLE_CTP
