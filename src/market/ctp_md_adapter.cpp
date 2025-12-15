/**
 * @file ctp_md_adapter.cpp
 * @brief CTP 行情适配器实现
 */

#ifdef ENABLE_CTP

#include "market/ctp_md_adapter.hpp"
#include "base/logger.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cfloat>

namespace fix40 {

// =============================================================================
// CtpMdSpi 实现
// =============================================================================

void CtpMdSpi::OnFrontConnected() {
    LOG() << "[CTP] 前置连接成功";
    adapter_->notifyState(MdAdapterState::CONNECTED, "前置连接成功");
    adapter_->doLogin();
}

void CtpMdSpi::OnFrontDisconnected(int nReason) {
    LOG() << "[CTP] 连接断开, 原因: " << nReason;
    adapter_->notifyState(MdAdapterState::DISCONNECTED, 
                          "连接断开, 原因: " + std::to_string(nReason));
}

void CtpMdSpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                              CThostFtdcRspInfoField* pRspInfo,
                              int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP] 登录失败, 错误码: " << pRspInfo->ErrorID 
              << ", 错误信息: " << pRspInfo->ErrorMsg;
        adapter_->notifyState(MdAdapterState::ERROR, 
                              std::string("登录失败: ") + pRspInfo->ErrorMsg);
        return;
    }

    LOG() << "[CTP] 登录成功";
    if (pRspUserLogin) {
        std::lock_guard<std::mutex> lock(adapter_->mutex_);
        adapter_->tradingDay_ = pRspUserLogin->TradingDay;
        LOG() << "[CTP] 交易日: " << adapter_->tradingDay_;
    }

    adapter_->notifyState(MdAdapterState::READY, "登录成功");
    adapter_->doSubscribePending();
}

void CtpMdSpi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                                   CThostFtdcRspInfoField* pRspInfo,
                                   int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP] 订阅失败, 错误码: " << pRspInfo->ErrorID
              << ", 错误信息: " << pRspInfo->ErrorMsg;
        return;
    }
    if (pSpecificInstrument) {
        LOG() << "[CTP] 订阅成功: " << pSpecificInstrument->InstrumentID;
        std::lock_guard<std::mutex> lock(adapter_->mutex_);
        adapter_->subscribedInstruments_.insert(pSpecificInstrument->InstrumentID);
    }
}

void CtpMdSpi::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                                     CThostFtdcRspInfoField* pRspInfo,
                                     int nRequestID, bool bIsLast) {
    if (pRspInfo && pRspInfo->ErrorID != 0) {
        LOG() << "[CTP] 退订失败, 错误码: " << pRspInfo->ErrorID;
        return;
    }
    if (pSpecificInstrument) {
        LOG() << "[CTP] 退订成功: " << pSpecificInstrument->InstrumentID;
        std::lock_guard<std::mutex> lock(adapter_->mutex_);
        adapter_->subscribedInstruments_.erase(pSpecificInstrument->InstrumentID);
    }
}

void CtpMdSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) {
    if (!pDepthMarketData) return;
    
    MarketData md = CtpMdAdapter::convertMarketData(pDepthMarketData);
    adapter_->pushMarketData(std::move(md));
}

void CtpMdSpi::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    if (pRspInfo) {
        LOG() << "[CTP] 错误, 错误码: " << pRspInfo->ErrorID
              << ", 错误信息: " << pRspInfo->ErrorMsg;
    }
}

void CtpMdSpi::OnHeartBeatWarning(int nTimeLapse) {
    LOG() << "[CTP] 心跳超时警告, 距上次: " << nTimeLapse << "秒";
}

// =============================================================================
// CtpMdAdapter 实现
// =============================================================================

CtpMdAdapter::CtpMdAdapter(moodycamel::BlockingConcurrentQueue<MarketData>& queue,
                           const CtpMdConfig& config)
    : MdAdapter(queue), config_(config) {
}

CtpMdAdapter::~CtpMdAdapter() {
    stop();
}

bool CtpMdAdapter::start() {
    if (running_.load()) {
        return true;
    }

    LOG() << "[CTP] 启动行情适配器";
    LOG() << "[CTP] 前置地址: " << config_.mdFront;
    LOG() << "[CTP] BrokerID: " << config_.brokerId;

    // 创建流文件目录
    if (!config_.flowPath.empty()) {
        std::string cmd = "mkdir -p " + config_.flowPath;
        system(cmd.c_str());
    }

    // 创建 API
    api_ = CThostFtdcMdApi::CreateFtdcMdApi(config_.flowPath.c_str());
    if (!api_) {
        LOG() << "[CTP] 创建 MdApi 失败";
        return false;
    }

    // 创建 SPI
    spi_ = std::make_unique<CtpMdSpi>(this);
    api_->RegisterSpi(spi_.get());

    // 注册前置
    api_->RegisterFront(const_cast<char*>(config_.mdFront.c_str()));

    // 初始化
    running_ = true;
    state_ = MdAdapterState::CONNECTING;
    api_->Init();

    LOG() << "[CTP] API 初始化完成";
    return true;
}

void CtpMdAdapter::stop() {
    if (!running_.load()) {
        return;
    }

    LOG() << "[CTP] 停止行情适配器";
    running_ = false;

    if (api_) {
        api_->Release();
        api_ = nullptr;
    }

    spi_.reset();
    state_ = MdAdapterState::DISCONNECTED;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribedInstruments_.clear();
        pendingSubscribe_.clear();
    }

    LOG() << "[CTP] 行情适配器已停止";
}

bool CtpMdAdapter::subscribe(const std::vector<std::string>& instruments) {
    if (instruments.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 如果还没登录成功，先加入待订阅列表
    if (state_.load() != MdAdapterState::READY) {
        for (const auto& inst : instruments) {
            pendingSubscribe_.insert(inst);
        }
        LOG() << "[CTP] 添加 " << instruments.size() << " 个合约到待订阅列表";
        return true;
    }

    // 已登录，直接订阅
    std::vector<char*> instArray;
    for (const auto& inst : instruments) {
        instArray.push_back(const_cast<char*>(inst.c_str()));
    }

    int ret = api_->SubscribeMarketData(instArray.data(), static_cast<int>(instArray.size()));
    LOG() << "[CTP] 订阅 " << instruments.size() << " 个合约, 返回: " << ret;
    return ret == 0;
}

bool CtpMdAdapter::unsubscribe(const std::vector<std::string>& instruments) {
    if (instruments.empty() || !api_ || state_.load() != MdAdapterState::READY) {
        return false;
    }

    std::vector<char*> instArray;
    for (const auto& inst : instruments) {
        instArray.push_back(const_cast<char*>(inst.c_str()));
    }

    int ret = api_->UnSubscribeMarketData(instArray.data(), static_cast<int>(instArray.size()));
    LOG() << "[CTP] 退订 " << instruments.size() << " 个合约, 返回: " << ret;
    return ret == 0;
}

void CtpMdAdapter::setStateCallback(StateCallback callback) {
    stateCallback_ = std::move(callback);
}

std::string CtpMdAdapter::getTradingDay() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tradingDay_;
}

void CtpMdAdapter::doLogin() {
    CThostFtdcReqUserLoginField req{};
    std::strncpy(req.BrokerID, config_.brokerId.c_str(), sizeof(req.BrokerID) - 1);
    std::strncpy(req.UserID, config_.userId.c_str(), sizeof(req.UserID) - 1);
    std::strncpy(req.Password, config_.password.c_str(), sizeof(req.Password) - 1);

    state_ = MdAdapterState::LOGGING_IN;
    int ret = api_->ReqUserLogin(&req, ++requestId_);
    LOG() << "[CTP] 发送登录请求, 返回: " << ret;
}

void CtpMdAdapter::doSubscribePending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingSubscribe_.empty()) {
        return;
    }

    std::vector<char*> instArray;
    std::vector<std::string> instList(pendingSubscribe_.begin(), pendingSubscribe_.end());
    for (auto& inst : instList) {
        instArray.push_back(const_cast<char*>(inst.c_str()));
    }

    int ret = api_->SubscribeMarketData(instArray.data(), static_cast<int>(instArray.size()));
    LOG() << "[CTP] 订阅待订阅列表中的 " << instArray.size() << " 个合约, 返回: " << ret;
    pendingSubscribe_.clear();
}

void CtpMdAdapter::notifyState(MdAdapterState state, const std::string& message) {
    state_ = state;
    if (stateCallback_) {
        stateCallback_(state, message);
    }
}

MarketData CtpMdAdapter::convertMarketData(const CThostFtdcDepthMarketDataField* p) {
    MarketData md;

    // 合约标识
    md.setInstrumentID(p->InstrumentID);
    md.setExchangeID(p->ExchangeID);
    md.setTradingDay(p->TradingDay);
    md.setUpdateTime(p->UpdateTime);
    md.updateMillisec = p->UpdateMillisec;

    // 价格信息 (CTP 用 DBL_MAX 表示无效价格)
    auto validPrice = [](double price) {
        return price < DBL_MAX / 2 ? price : 0.0;
    };

    md.lastPrice = validPrice(p->LastPrice);
    md.preSettlementPrice = validPrice(p->PreSettlementPrice);
    md.preClosePrice = validPrice(p->PreClosePrice);
    md.openPrice = validPrice(p->OpenPrice);
    md.highestPrice = validPrice(p->HighestPrice);
    md.lowestPrice = validPrice(p->LowestPrice);
    md.closePrice = validPrice(p->ClosePrice);
    md.settlementPrice = validPrice(p->SettlementPrice);
    md.upperLimitPrice = validPrice(p->UpperLimitPrice);
    md.lowerLimitPrice = validPrice(p->LowerLimitPrice);
    md.averagePrice = validPrice(p->AveragePrice);

    // 成交信息
    md.volume = p->Volume;
    md.turnover = p->Turnover;
    md.openInterest = p->OpenInterest;
    md.preOpenInterest = p->PreOpenInterest;

    // 五档盘口
    md.bidPrice1 = validPrice(p->BidPrice1);
    md.bidVolume1 = p->BidVolume1;
    md.askPrice1 = validPrice(p->AskPrice1);
    md.askVolume1 = p->AskVolume1;

    md.bidPrice2 = validPrice(p->BidPrice2);
    md.bidVolume2 = p->BidVolume2;
    md.askPrice2 = validPrice(p->AskPrice2);
    md.askVolume2 = p->AskVolume2;

    md.bidPrice3 = validPrice(p->BidPrice3);
    md.bidVolume3 = p->BidVolume3;
    md.askPrice3 = validPrice(p->AskPrice3);
    md.askVolume3 = p->AskVolume3;

    md.bidPrice4 = validPrice(p->BidPrice4);
    md.bidVolume4 = p->BidVolume4;
    md.askPrice4 = validPrice(p->AskPrice4);
    md.askVolume4 = p->AskVolume4;

    md.bidPrice5 = validPrice(p->BidPrice5);
    md.bidVolume5 = p->BidVolume5;
    md.askPrice5 = validPrice(p->AskPrice5);
    md.askVolume5 = p->AskVolume5;

    return md;
}

// =============================================================================
// 配置加载
// =============================================================================

CtpMdConfig loadCtpConfig(const std::string& filename) {
    CtpMdConfig config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG() << "[CTP] 无法打开配置文件: " << filename;
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

        if (key == "md_front") config.mdFront = value;
        else if (key == "broker_id") config.brokerId = value;
        else if (key == "user_id") config.userId = value;
        else if (key == "password") config.password = value;
        else if (key == "flow_path") config.flowPath = value;
    }

    // 默认流文件路径
    if (config.flowPath.empty()) {
        config.flowPath = "./ctp_md_flow/";
    }

    return config;
}

} // namespace fix40

#endif // ENABLE_CTP
