/**
 * @file instrument_manager.cpp
 * @brief 合约信息管理模块实现
 */

#include "app/manager/instrument_manager.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace fix40 {

// =============================================================================
// 简单JSON解析辅助函数
// =============================================================================

namespace {

/**
 * @brief 跳过空白字符
 */
void skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.size() && std::isspace(json[pos])) {
        ++pos;
    }
}

/**
 * @brief 解析字符串值
 */
std::string parseString(const std::string& json, size_t& pos) {
    if (json[pos] != '"') {
        throw std::runtime_error("Expected '\"' at position " + std::to_string(pos));
    }
    ++pos;
    
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    
    if (pos >= json.size()) {
        throw std::runtime_error("Unterminated string");
    }
    ++pos; // skip closing quote
    return result;
}

/**
 * @brief 解析数字值
 */
double parseNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    if (json[pos] == '-') ++pos;
    while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.')) {
        ++pos;
    }
    return std::stod(json.substr(start, pos - start));
}

/**
 * @brief 解析单个合约对象
 */
Instrument parseInstrumentObject(const std::string& json, size_t& pos) {
    Instrument inst;
    
    skipWhitespace(json, pos);
    if (json[pos] != '{') {
        throw std::runtime_error("Expected '{' at position " + std::to_string(pos));
    }
    ++pos;
    
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (json[pos] == '}') {
            ++pos;
            break;
        }
        
        if (json[pos] == ',') {
            ++pos;
            skipWhitespace(json, pos);
        }
        
        // 解析键
        std::string key = parseString(json, pos);
        
        skipWhitespace(json, pos);
        if (json[pos] != ':') {
            throw std::runtime_error("Expected ':' after key");
        }
        ++pos;
        skipWhitespace(json, pos);
        
        // 解析值
        if (key == "instrumentId") {
            inst.instrumentId = parseString(json, pos);
        } else if (key == "exchangeId") {
            inst.exchangeId = parseString(json, pos);
        } else if (key == "productId") {
            inst.productId = parseString(json, pos);
        } else if (key == "priceTick") {
            inst.priceTick = parseNumber(json, pos);
        } else if (key == "volumeMultiple") {
            inst.volumeMultiple = static_cast<int>(parseNumber(json, pos));
        } else if (key == "marginRate") {
            inst.marginRate = parseNumber(json, pos);
        } else if (key == "upperLimitPrice") {
            inst.upperLimitPrice = parseNumber(json, pos);
        } else if (key == "lowerLimitPrice") {
            inst.lowerLimitPrice = parseNumber(json, pos);
        } else if (key == "preSettlementPrice") {
            inst.preSettlementPrice = parseNumber(json, pos);
        } else {
            // 跳过未知字段
            if (json[pos] == '"') {
                parseString(json, pos);
            } else if (json[pos] == '-' || std::isdigit(json[pos])) {
                parseNumber(json, pos);
            }
        }
    }
    
    return inst;
}

/**
 * @brief 解析合约数组
 */
std::vector<Instrument> parseInstrumentsArray(const std::string& json, size_t& pos) {
    std::vector<Instrument> instruments;
    
    skipWhitespace(json, pos);
    if (json[pos] != '[') {
        throw std::runtime_error("Expected '[' at position " + std::to_string(pos));
    }
    ++pos;
    
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (json[pos] == ']') {
            ++pos;
            break;
        }
        
        if (json[pos] == ',') {
            ++pos;
            skipWhitespace(json, pos);
        }
        
        instruments.push_back(parseInstrumentObject(json, pos));
    }
    
    return instruments;
}

} // anonymous namespace

// =============================================================================
// InstrumentManager 实现
// =============================================================================

bool InstrumentManager::loadFromConfig(const std::string& configPath) {
    // 读取文件内容
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    
    try {
        size_t pos = 0;
        skipWhitespace(json, pos);
        
        if (json[pos] != '{') {
            return false;
        }
        ++pos;
        
        while (pos < json.size()) {
            skipWhitespace(json, pos);
            if (json[pos] == '}') {
                break;
            }
            
            if (json[pos] == ',') {
                ++pos;
                skipWhitespace(json, pos);
            }
            
            // 解析键
            std::string key = parseString(json, pos);
            
            skipWhitespace(json, pos);
            if (json[pos] != ':') {
                return false;
            }
            ++pos;
            skipWhitespace(json, pos);
            
            if (key == "instruments") {
                auto instruments = parseInstrumentsArray(json, pos);
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& inst : instruments) {
                    instruments_[inst.instrumentId] = inst;
                }
            } else {
                // 跳过其他字段 - 简单处理
                if (json[pos] == '"') {
                    parseString(json, pos);
                } else if (json[pos] == '[') {
                    int depth = 1;
                    ++pos;
                    while (pos < json.size() && depth > 0) {
                        if (json[pos] == '[') ++depth;
                        else if (json[pos] == ']') --depth;
                        ++pos;
                    }
                } else if (json[pos] == '{') {
                    int depth = 1;
                    ++pos;
                    while (pos < json.size() && depth > 0) {
                        if (json[pos] == '{') ++depth;
                        else if (json[pos] == '}') --depth;
                        ++pos;
                    }
                }
            }
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void InstrumentManager::addInstrument(const Instrument& instrument) {
    std::lock_guard<std::mutex> lock(mutex_);
    instruments_[instrument.instrumentId] = instrument;
}

void InstrumentManager::addInstruments(const std::vector<Instrument>& instruments) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& inst : instruments) {
        instruments_[inst.instrumentId] = inst;
    }
}

const Instrument* InstrumentManager::getInstrument(const std::string& instrumentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instruments_.find(instrumentId);
    if (it != instruments_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::optional<Instrument> InstrumentManager::getInstrumentCopy(const std::string& instrumentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instruments_.find(instrumentId);
    if (it != instruments_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool InstrumentManager::hasInstrument(const std::string& instrumentId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return instruments_.find(instrumentId) != instruments_.end();
}

std::vector<std::string> InstrumentManager::getAllInstrumentIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(instruments_.size());
    for (const auto& pair : instruments_) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t InstrumentManager::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return instruments_.size();
}

bool InstrumentManager::updateLimitPrices(const std::string& instrumentId,
                                           double upperLimit, double lowerLimit) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instruments_.find(instrumentId);
    if (it == instruments_.end()) {
        return false;
    }
    it->second.updateLimitPrices(upperLimit, lowerLimit);
    return true;
}

bool InstrumentManager::updatePreSettlementPrice(const std::string& instrumentId,
                                                  double preSettlementPrice) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = instruments_.find(instrumentId);
    if (it == instruments_.end()) {
        return false;
    }
    it->second.preSettlementPrice = preSettlementPrice;
    return true;
}

void InstrumentManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    instruments_.clear();
}

std::vector<std::string> InstrumentManager::searchByPrefix(const std::string& prefix, size_t limit) const {
    std::vector<std::string> results;
    
    if (prefix.empty()) {
        return results;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 收集所有匹配的合约
    for (const auto& [id, inst] : instruments_) {
        if (id.size() >= prefix.size() && 
            id.compare(0, prefix.size(), prefix) == 0) {
            results.push_back(id);
        }
    }
    
    // 按字母排序
    std::sort(results.begin(), results.end());
    
    // 限制返回数量
    if (results.size() > limit) {
        results.resize(limit);
    }
    
    return results;
}

std::vector<std::string> InstrumentManager::getInstrumentsByProduct(const std::string& productId) const {
    std::vector<std::string> results;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [id, inst] : instruments_) {
        if (inst.productId == productId) {
            results.push_back(id);
        }
    }
    
    std::sort(results.begin(), results.end());
    return results;
}

std::vector<std::string> InstrumentManager::getInstrumentsByExchange(const std::string& exchangeId) const {
    std::vector<std::string> results;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [id, inst] : instruments_) {
        if (inst.exchangeId == exchangeId) {
            results.push_back(id);
        }
    }
    
    std::sort(results.begin(), results.end());
    return results;
}

} // namespace fix40
