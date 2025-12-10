/**
 * @file market_data.hpp
 * @brief 行情数据结构定义
 *
 * 定义内部使用的行情数据 POD 结构体，与外部数据源（如 CTP）解耦。
 * 所有外部行情数据都应转换为此格式后再进入系统。
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace fix40 {

/**
 * @brief 合约代码最大长度
 */
constexpr size_t INSTRUMENT_ID_LEN = 32;

/**
 * @brief 交易所代码最大长度
 */
constexpr size_t EXCHANGE_ID_LEN = 16;

/**
 * @brief 日期字符串长度 (YYYYMMDD)
 */
constexpr size_t DATE_LEN = 9;

/**
 * @brief 时间字符串长度 (HH:MM:SS)
 */
constexpr size_t TIME_LEN = 9;

/**
 * @struct MarketData
 * @brief 行情数据 POD 结构体
 *
 * 设计原则：
 * - POD 类型，可安全地在无锁队列中传递
 * - 使用固定长度字符数组，避免动态内存分配
 * - 字段命名清晰，与业务含义对应
 * - 与外部数据源（CTP 等）解耦
 *
 * @par 价格精度
 * 所有价格字段使用 double 类型，精度由具体合约决定。
 * 无效价格使用 0.0 或 DBL_MAX 表示。
 *
 * @par 线程安全
 * 结构体本身是值类型，可安全复制。
 * 在多线程环境中通过无锁队列传递。
 */
struct MarketData {
    // =========================================================================
    // 合约标识
    // =========================================================================
    
    char instrumentID[INSTRUMENT_ID_LEN];  ///< 合约代码
    char exchangeID[EXCHANGE_ID_LEN];      ///< 交易所代码
    char tradingDay[DATE_LEN];             ///< 交易日 (YYYYMMDD)
    char updateTime[TIME_LEN];             ///< 更新时间 (HH:MM:SS)
    int32_t updateMillisec;                ///< 更新毫秒数

    // =========================================================================
    // 价格信息
    // =========================================================================
    
    double lastPrice;           ///< 最新价
    double preSettlementPrice;  ///< 昨结算价
    double preClosePrice;       ///< 昨收盘价
    double openPrice;           ///< 开盘价
    double highestPrice;        ///< 最高价
    double lowestPrice;         ///< 最低价
    double closePrice;          ///< 收盘价
    double settlementPrice;     ///< 结算价
    double upperLimitPrice;     ///< 涨停价
    double lowerLimitPrice;     ///< 跌停价
    double averagePrice;        ///< 均价

    // =========================================================================
    // 成交信息
    // =========================================================================
    
    int64_t volume;             ///< 成交量
    double turnover;            ///< 成交额
    double openInterest;        ///< 持仓量
    double preOpenInterest;     ///< 昨持仓量

    // =========================================================================
    // 买卖盘（5档）
    // =========================================================================
    
    double bidPrice1;           ///< 买一价
    int32_t bidVolume1;         ///< 买一量
    double askPrice1;           ///< 卖一价
    int32_t askVolume1;         ///< 卖一量

    double bidPrice2;           ///< 买二价
    int32_t bidVolume2;         ///< 买二量
    double askPrice2;           ///< 卖二价
    int32_t askVolume2;         ///< 卖二量

    double bidPrice3;           ///< 买三价
    int32_t bidVolume3;         ///< 买三量
    double askPrice3;           ///< 卖三价
    int32_t askVolume3;         ///< 卖三量

    double bidPrice4;           ///< 买四价
    int32_t bidVolume4;         ///< 买四量
    double askPrice4;           ///< 卖四价
    int32_t askVolume4;         ///< 卖四量

    double bidPrice5;           ///< 买五价
    int32_t bidVolume5;         ///< 买五量
    double askPrice5;           ///< 卖五价
    int32_t askVolume5;         ///< 卖五量

    // =========================================================================
    // 构造函数
    // =========================================================================

    /**
     * @brief 默认构造函数，初始化所有字段为零值
     */
    MarketData() {
        std::memset(this, 0, sizeof(MarketData));
    }

    /**
     * @brief 设置合约代码
     * @param id 合约代码字符串
     */
    void setInstrumentID(const char* id) {
        std::strncpy(instrumentID, id, INSTRUMENT_ID_LEN - 1);
        instrumentID[INSTRUMENT_ID_LEN - 1] = '\0';
    }

    /**
     * @brief 设置交易所代码
     * @param id 交易所代码字符串
     */
    void setExchangeID(const char* id) {
        std::strncpy(exchangeID, id, EXCHANGE_ID_LEN - 1);
        exchangeID[EXCHANGE_ID_LEN - 1] = '\0';
    }

    /**
     * @brief 设置交易日
     * @param day 交易日字符串 (YYYYMMDD)
     */
    void setTradingDay(const char* day) {
        std::strncpy(tradingDay, day, DATE_LEN - 1);
        tradingDay[DATE_LEN - 1] = '\0';
    }

    /**
     * @brief 设置更新时间
     * @param time 时间字符串 (HH:MM:SS)
     */
    void setUpdateTime(const char* time) {
        std::strncpy(updateTime, time, TIME_LEN - 1);
        updateTime[TIME_LEN - 1] = '\0';
    }

    /**
     * @brief 获取合约代码字符串
     */
    std::string getInstrumentID() const {
        return std::string(instrumentID);
    }

    /**
     * @brief 获取交易所代码字符串
     */
    std::string getExchangeID() const {
        return std::string(exchangeID);
    }
};

// 静态断言确保 POD 特性
static_assert(std::is_trivially_copyable<MarketData>::value, 
              "MarketData must be trivially copyable for lock-free queue");

} // namespace fix40
