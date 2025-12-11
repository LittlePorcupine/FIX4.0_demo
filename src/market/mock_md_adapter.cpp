/**
 * @file mock_md_adapter.cpp
 * @brief 模拟行情适配器实现
 */

#include "market/mock_md_adapter.hpp"
#include "base/logger.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>

namespace fix40 {

namespace {
// 线程安全的 localtime 封装
std::tm safe_localtime(std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    return tm;
}
} // anonymous namespace

MockMdAdapter::MockMdAdapter(moodycamel::BlockingConcurrentQueue<MarketData>& queue)
    : MdAdapter(queue)
    , rng_(std::random_device{}())
    , tradingDay_([]() {
        // 生成模拟交易日（当前日期）
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = safe_localtime(time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d");
        return oss.str();
    }())
{
}

MockMdAdapter::~MockMdAdapter() {
    stop();
}

bool MockMdAdapter::start() {
    if (running_.load()) {
        return true;
    }

    notifyState(MdAdapterState::CONNECTING, "Connecting to mock data source...");
    
    running_.store(true);
    state_.store(MdAdapterState::READY);
    
    workerThread_ = std::thread(&MockMdAdapter::run, this);
    
    notifyState(MdAdapterState::READY, "Mock adapter ready");
    LOG() << "[MockMdAdapter] Started, trading day: " << tradingDay_;
    
    return true;
}

void MockMdAdapter::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    state_.store(MdAdapterState::DISCONNECTED);
    notifyState(MdAdapterState::DISCONNECTED, "Mock adapter stopped");
    LOG() << "[MockMdAdapter] Stopped";
}

bool MockMdAdapter::subscribe(const std::vector<std::string>& instruments) {
    if (state_.load() != MdAdapterState::READY) {
        LOG() << "[MockMdAdapter] Cannot subscribe: not ready";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& inst : instruments) {
        subscribedInstruments_.insert(inst);
        // 如果没有设置基准价，使用默认值
        if (basePrices_.find(inst) == basePrices_.end()) {
            basePrices_[inst] = 5000.0;
        }
        if (lastPrices_.find(inst) == lastPrices_.end()) {
            lastPrices_[inst] = basePrices_[inst];
        }
        LOG() << "[MockMdAdapter] Subscribed: " << inst;
    }
    
    return true;
}

bool MockMdAdapter::unsubscribe(const std::vector<std::string>& instruments) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& inst : instruments) {
        subscribedInstruments_.erase(inst);
        LOG() << "[MockMdAdapter] Unsubscribed: " << inst;
    }
    return true;
}

void MockMdAdapter::setStateCallback(StateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    stateCallback_ = std::move(callback);
}

std::string MockMdAdapter::getTradingDay() const {
    return tradingDay_;
}

void MockMdAdapter::setBasePrice(const std::string& instrument, double basePrice) {
    std::lock_guard<std::mutex> lock(mutex_);
    basePrices_[instrument] = basePrice;
    if (lastPrices_.find(instrument) == lastPrices_.end()) {
        lastPrices_[instrument] = basePrice;
    }
}

void MockMdAdapter::run() {
    while (running_.load()) {
        std::vector<std::string> instruments;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            instruments.assign(subscribedInstruments_.begin(), 
                             subscribedInstruments_.end());
        }

        for (const auto& inst : instruments) {
            if (!running_.load()) break;
            
            MarketData md = generateTick(inst);
            pushMarketData(std::move(md));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(tickIntervalMs_.load()));
    }
}

MarketData MockMdAdapter::generateTick(const std::string& instrument) {
    MarketData md;
    
    // 设置合约标识
    md.setInstrumentID(instrument.c_str());
    md.setExchangeID("MOCK");
    md.setTradingDay(tradingDay_.c_str());
    md.setUpdateTime(getCurrentTime().c_str());
    md.updateMillisec = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() % 1000;

    // 获取基准价和上次价格
    double basePrice = 5000.0;
    double lastPrice = 5000.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = basePrices_.find(instrument);
        if (it != basePrices_.end()) {
            basePrice = it->second;
        }
        auto lit = lastPrices_.find(instrument);
        if (lit != lastPrices_.end()) {
            lastPrice = lit->second;
        }
    }

    // 生成随机价格变动
    const double vol = volatility_.load();
    std::uniform_real_distribution<double> dist(-vol, vol);
    double change = dist(rng_);
    double newPrice = lastPrice * (1.0 + change);
    
    // 限制价格在基准价的 ±10% 范围内
    double upperLimit = basePrice * 1.10;
    double lowerLimit = basePrice * 0.90;
    newPrice = std::max(lowerLimit, std::min(upperLimit, newPrice));

    // 更新最新价
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastPrices_[instrument] = newPrice;
    }

    // 设置价格信息
    md.lastPrice = newPrice;
    md.preSettlementPrice = basePrice;
    md.preClosePrice = basePrice * 0.998;
    md.openPrice = basePrice * 1.001;
    md.highestPrice = std::max(newPrice, basePrice * 1.02);
    md.lowestPrice = std::min(newPrice, basePrice * 0.98);
    md.closePrice = 0.0;  // 未收盘
    md.settlementPrice = 0.0;  // 未结算
    md.upperLimitPrice = upperLimit;
    md.lowerLimitPrice = lowerLimit;
    md.averagePrice = basePrice;

    // 生成成交信息
    std::uniform_int_distribution<int64_t> volDist(100, 10000);
    md.volume = volDist(rng_);
    md.turnover = md.volume * newPrice;
    md.openInterest = volDist(rng_) * 10;
    md.preOpenInterest = md.openInterest * 0.95;

    // 生成买卖盘
    double spread = basePrice * 0.0002;  // 0.02% 价差
    std::uniform_int_distribution<int32_t> qtyDist(10, 500);

    md.bidPrice1 = newPrice - spread;
    md.bidVolume1 = qtyDist(rng_);
    md.askPrice1 = newPrice + spread;
    md.askVolume1 = qtyDist(rng_);

    md.bidPrice2 = md.bidPrice1 - spread;
    md.bidVolume2 = qtyDist(rng_);
    md.askPrice2 = md.askPrice1 + spread;
    md.askVolume2 = qtyDist(rng_);

    md.bidPrice3 = md.bidPrice2 - spread;
    md.bidVolume3 = qtyDist(rng_);
    md.askPrice3 = md.askPrice2 + spread;
    md.askVolume3 = qtyDist(rng_);

    md.bidPrice4 = md.bidPrice3 - spread;
    md.bidVolume4 = qtyDist(rng_);
    md.askPrice4 = md.askPrice3 + spread;
    md.askVolume4 = qtyDist(rng_);

    md.bidPrice5 = md.bidPrice4 - spread;
    md.bidVolume5 = qtyDist(rng_);
    md.askPrice5 = md.askPrice4 + spread;
    md.askVolume5 = qtyDist(rng_);

    return md;
}

void MockMdAdapter::notifyState(MdAdapterState state, const std::string& message) {
    StateCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = stateCallback_;
    }
    
    if (callback) {
        callback(state, message);
    }
}

std::string MockMdAdapter::getCurrentTime() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = safe_localtime(time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S");
    return oss.str();
}

} // namespace fix40
