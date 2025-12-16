/**
 * @file test_instrument_manager.cpp
 * @brief InstrumentManager 单元测试
 *
 * 测试合约管理器的加载、查询、更新功能。
 */

#include "../catch2/catch.hpp"
#include "app/instrument_manager.hpp"
#include <fstream>
#include <cstdio>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace fix40;

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * @brief 创建临时配置文件
 *
 * 使用 std::random_device 和时间戳生成唯一文件名，
 * 确保并行测试时不会发生文件名冲突。
 */
std::string createTempConfigFile(const std::string& content) {
    // 使用随机设备和时间戳组合生成唯一文件名
    std::random_device rd;
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "test_instruments_" << timestamp << "_" << rd() << ".json";
    std::string filename = oss.str();
    
    std::ofstream file(filename);
    file << content;
    file.close();
    return filename;
}

/**
 * @brief 删除临时文件
 */
void removeTempFile(const std::string& filename) {
    std::remove(filename.c_str());
}

// =============================================================================
// 单元测试
// =============================================================================

TEST_CASE("InstrumentManager 默认构造", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    REQUIRE(mgr.size() == 0);
    REQUIRE(mgr.getAllInstrumentIds().empty());
}

TEST_CASE("InstrumentManager addInstrument 添加单个合约", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    REQUIRE(mgr.size() == 1);
    REQUIRE(mgr.hasInstrument("IF2601"));
    
    const Instrument* retrieved = mgr.getInstrument("IF2601");
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->instrumentId == "IF2601");
    REQUIRE(retrieved->exchangeId == "CFFEX");
    REQUIRE(retrieved->volumeMultiple == 300);
}

TEST_CASE("InstrumentManager addInstruments 批量添加合约", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    std::vector<Instrument> instruments = {
        Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12),
        Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12),
        Instrument("IH2601", "CFFEX", "IH", 0.2, 300, 0.10)
    };
    
    mgr.addInstruments(instruments);
    
    REQUIRE(mgr.size() == 3);
    REQUIRE(mgr.hasInstrument("IF2601"));
    REQUIRE(mgr.hasInstrument("IC2601"));
    REQUIRE(mgr.hasInstrument("IH2601"));
}

TEST_CASE("InstrumentManager getInstrument 查询合约", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    SECTION("查询存在的合约") {
        const Instrument* retrieved = mgr.getInstrument("IF2601");
        REQUIRE(retrieved != nullptr);
        REQUIRE(retrieved->instrumentId == "IF2601");
    }
    
    SECTION("查询不存在的合约") {
        const Instrument* retrieved = mgr.getInstrument("UNKNOWN");
        REQUIRE(retrieved == nullptr);
    }
}

TEST_CASE("InstrumentManager getInstrumentCopy 获取合约副本", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    SECTION("获取存在的合约副本") {
        auto copy = mgr.getInstrumentCopy("IF2601");
        REQUIRE(copy.has_value());
        REQUIRE(copy->instrumentId == "IF2601");
    }
    
    SECTION("获取不存在的合约副本") {
        auto copy = mgr.getInstrumentCopy("UNKNOWN");
        REQUIRE(!copy.has_value());
    }
}

TEST_CASE("InstrumentManager hasInstrument 检查合约存在", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    REQUIRE(mgr.hasInstrument("IF2601") == true);
    REQUIRE(mgr.hasInstrument("UNKNOWN") == false);
}

TEST_CASE("InstrumentManager getAllInstrumentIds 获取所有合约代码", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    mgr.addInstrument(Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
    mgr.addInstrument(Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
    
    auto ids = mgr.getAllInstrumentIds();
    
    REQUIRE(ids.size() == 2);
    // 由于unordered_map顺序不确定，检查包含关系
    REQUIRE(std::find(ids.begin(), ids.end(), "IF2601") != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), "IC2601") != ids.end());
}

TEST_CASE("InstrumentManager updateLimitPrices 更新涨跌停价格", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    SECTION("更新存在的合约") {
        bool result = mgr.updateLimitPrices("IF2601", 4400.0, 3600.0);
        REQUIRE(result == true);
        
        const Instrument* retrieved = mgr.getInstrument("IF2601");
        REQUIRE(retrieved->upperLimitPrice == 4400.0);
        REQUIRE(retrieved->lowerLimitPrice == 3600.0);
    }
    
    SECTION("更新不存在的合约") {
        bool result = mgr.updateLimitPrices("UNKNOWN", 4400.0, 3600.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("InstrumentManager updatePreSettlementPrice 更新昨结算价", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst);
    
    SECTION("更新存在的合约") {
        bool result = mgr.updatePreSettlementPrice("IF2601", 4000.0);
        REQUIRE(result == true);
        
        const Instrument* retrieved = mgr.getInstrument("IF2601");
        REQUIRE(retrieved->preSettlementPrice == 4000.0);
    }
    
    SECTION("更新不存在的合约") {
        bool result = mgr.updatePreSettlementPrice("UNKNOWN", 4000.0);
        REQUIRE(result == false);
    }
}

TEST_CASE("InstrumentManager clear 清空合约", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    mgr.addInstrument(Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
    mgr.addInstrument(Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
    
    REQUIRE(mgr.size() == 2);
    
    mgr.clear();
    
    REQUIRE(mgr.size() == 0);
    REQUIRE(!mgr.hasInstrument("IF2601"));
    REQUIRE(!mgr.hasInstrument("IC2601"));
}

TEST_CASE("InstrumentManager loadFromConfig 从配置文件加载", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    SECTION("加载有效配置文件") {
        std::string config = R"({
            "instruments": [
                {
                    "instrumentId": "IF2601",
                    "exchangeId": "CFFEX",
                    "productId": "IF",
                    "priceTick": 0.2,
                    "volumeMultiple": 300,
                    "marginRate": 0.12
                },
                {
                    "instrumentId": "IC2601",
                    "exchangeId": "CFFEX",
                    "productId": "IC",
                    "priceTick": 0.2,
                    "volumeMultiple": 200,
                    "marginRate": 0.12
                }
            ]
        })";
        
        std::string filename = createTempConfigFile(config);
        
        bool result = mgr.loadFromConfig(filename);
        
        REQUIRE(result == true);
        REQUIRE(mgr.size() == 2);
        
        const Instrument* inst1 = mgr.getInstrument("IF2601");
        REQUIRE(inst1 != nullptr);
        REQUIRE(inst1->exchangeId == "CFFEX");
        REQUIRE(inst1->volumeMultiple == 300);
        REQUIRE(inst1->marginRate == Approx(0.12));
        
        const Instrument* inst2 = mgr.getInstrument("IC2601");
        REQUIRE(inst2 != nullptr);
        REQUIRE(inst2->volumeMultiple == 200);
        
        removeTempFile(filename);
    }
    
    SECTION("加载不存在的文件") {
        bool result = mgr.loadFromConfig("nonexistent_file.json");
        REQUIRE(result == false);
    }
    
    SECTION("加载带涨跌停价格的配置") {
        std::string config = R"({
            "instruments": [
                {
                    "instrumentId": "IF2601",
                    "exchangeId": "CFFEX",
                    "productId": "IF",
                    "priceTick": 0.2,
                    "volumeMultiple": 300,
                    "marginRate": 0.12,
                    "upperLimitPrice": 4400.0,
                    "lowerLimitPrice": 3600.0,
                    "preSettlementPrice": 4000.0
                }
            ]
        })";
        
        std::string filename = createTempConfigFile(config);
        
        bool result = mgr.loadFromConfig(filename);
        
        REQUIRE(result == true);
        
        const Instrument* inst = mgr.getInstrument("IF2601");
        REQUIRE(inst != nullptr);
        REQUIRE(inst->upperLimitPrice == 4400.0);
        REQUIRE(inst->lowerLimitPrice == 3600.0);
        REQUIRE(inst->preSettlementPrice == 4000.0);
        
        removeTempFile(filename);
    }
}

TEST_CASE("InstrumentManager 覆盖已存在的合约", "[instrument_manager][unit]") {
    InstrumentManager mgr;
    
    Instrument inst1("IF2601", "CFFEX", "IF", 0.2, 300, 0.12);
    mgr.addInstrument(inst1);
    
    // 添加同ID但不同参数的合约
    Instrument inst2("IF2601", "CFFEX", "IF", 0.2, 300, 0.15);  // 不同的保证金率
    mgr.addInstrument(inst2);
    
    REQUIRE(mgr.size() == 1);  // 仍然只有一个合约
    
    const Instrument* retrieved = mgr.getInstrument("IF2601");
    REQUIRE(retrieved->marginRate == Approx(0.15));  // 应该是新的值
}
