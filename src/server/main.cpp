/**
 * @file main.cpp
 * @brief FIX 模拟交易服务端入口点
 *
 * 启动流程：
 * 1. 加载配置文件（config.ini 和 simnow.ini）
 * 2. 连接 CTP 交易前置，查询合约列表
 * 3. 连接 CTP 行情前置，订阅行情
 * 4. 启动 FIX 服务器，等待客户端连接
 * 5. 行情驱动撮合和账户价值更新
 */

#include "server/server.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include "app/simulation_app.hpp"
#include "app/model/instrument.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>
#include <thread>
#include <atomic>
#include <fstream>
#include <map>

#ifdef ENABLE_CTP
#include "market/ctp_md_adapter.hpp"
#include "market/ctp_trader_adapter.hpp"
#include "base/blockingconcurrentqueue.h"
#endif

namespace {

// 全局停止标志
std::atomic<bool> g_running{true};

/**
 * @brief 简单的 INI 文件解析器
 */
std::map<std::string, std::string> parseIniFile(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return config;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            // 去除空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            config[key] = value;
        }
    }
    return config;
}

/**
 * @brief 查找配置文件路径
 */
std::string findConfigFile(const std::string& filename, const char* argv0) {
    // 1. 当前目录
    if (std::filesystem::exists(filename)) {
        return filename;
    }
    // 2. 可执行文件同级目录
    auto exePath = std::filesystem::path(argv0).parent_path() / filename;
    if (std::filesystem::exists(exePath)) {
        return exePath.string();
    }
    return "";
}

#ifdef ENABLE_CTP
/**
 * @brief 行情转发线程函数
 * 
 * 从行情队列读取数据，推送到撮合引擎
 */
void marketDataForwarder(
    moodycamel::BlockingConcurrentQueue<fix40::MarketData>& mdQueue,
    fix40::MatchingEngine& engine,
    std::atomic<bool>& running)
{
    LOG() << "[MarketDataForwarder] Started";
    fix40::MarketData md;
    
    while (running.load()) {
        // 带超时的等待，避免阻塞关闭
        if (mdQueue.wait_dequeue_timed(md, std::chrono::milliseconds(100))) {
            engine.submitMarketData(md);
        }
    }
    
    LOG() << "[MarketDataForwarder] Stopped";
}
#endif

} // anonymous namespace

/**
 * @brief 服务端主函数
 */
int main(int argc, char* argv[]) {
    // 忽略 SIGPIPE 信号
    signal(SIGPIPE, SIG_IGN);

    try {
        // =====================================================================
        // 1. 加载 config.ini
        // =====================================================================
        std::string configPath = findConfigFile("config.ini", argv[0]);
        if (configPath.empty()) {
            std::cerr << "Fatal: config.ini not found" << std::endl;
            return 1;
        }
        if (!fix40::Config::instance().load(configPath)) {
            std::cerr << "Fatal: Failed to load " << configPath << std::endl;
            return 1;
        }
        LOG() << "Config loaded from " << std::filesystem::absolute(configPath).string();

        // 解析命令行参数
        int port = fix40::Config::instance().get_int("server", "port", 9000);
        int numThreads = fix40::Config::instance().get_int("server", "default_threads", 0);
        if (argc > 1) numThreads = std::stoi(argv[1]);
        if (argc > 2) port = std::stoi(argv[2]);

        // =====================================================================
        // 2. 创建 SimulationApp
        // =====================================================================
        fix40::SimulationApp app;
        auto& instrumentMgr = app.getInstrumentManager();
        auto& engine = app.getMatchingEngine();

#ifdef ENABLE_CTP
        // =====================================================================
        // 3. 加载 simnow.ini 并连接 CTP
        // =====================================================================
        std::string simnowPath = findConfigFile("simnow.ini", argv[0]);
        if (simnowPath.empty()) {
            LOG() << "Warning: simnow.ini not found, using fallback test instruments";
            // 回退：使用测试合约
            instrumentMgr.addInstrument(fix40::Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
            instrumentMgr.addInstrument(fix40::Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
        } else {
            LOG() << "SimNow config loaded from " << simnowPath;
            auto simnowConfig = parseIniFile(simnowPath);
            
            // -----------------------------------------------------------------
            // 3.1 连接交易前置，查询合约列表
            // -----------------------------------------------------------------
            if (!simnowConfig["td_front"].empty()) {
                fix40::CtpTraderConfig traderConfig;
                traderConfig.traderFront = simnowConfig["td_front"];
                traderConfig.brokerId = simnowConfig["broker_id"];
                traderConfig.userId = simnowConfig["user_id"];
                traderConfig.password = simnowConfig["password"];
                traderConfig.appId = simnowConfig["app_id"];
                traderConfig.authCode = simnowConfig["auth_code"];
                traderConfig.flowPath = simnowConfig["trader_flow_path"];
                if (traderConfig.flowPath.empty()) {
                    traderConfig.flowPath = "./ctp_trader_flow/";
                }
                
                // 创建流文件目录
                std::filesystem::create_directories(traderConfig.flowPath);
                
                LOG() << "Connecting to CTP Trader: " << traderConfig.traderFront;
                fix40::CtpTraderAdapter traderAdapter(traderConfig);
                traderAdapter.setInstrumentManager(&instrumentMgr);
                
                traderAdapter.setStateCallback([](fix40::CtpTraderState state, const std::string& msg) {
                    LOG() << "[CtpTrader] State: " << static_cast<int>(state) << " - " << msg;
                });
                
                if (traderAdapter.start()) {
                    if (traderAdapter.waitForReady(15)) {
                        LOG() << "CTP Trader connected, querying instruments...";
                        traderAdapter.queryInstruments();
                        if (traderAdapter.waitForQueryComplete(60)) {
                            LOG() << "Loaded " << instrumentMgr.size() << " instruments from CTP";
                        } else {
                            LOG() << "Warning: Instrument query timeout";
                        }
                    } else {
                        LOG() << "Warning: CTP Trader connection timeout";
                    }
                    traderAdapter.stop();
                } else {
                    LOG() << "Warning: Failed to start CTP Trader adapter";
                }
            }
            
            // 如果没有查询到合约，使用回退
            if (instrumentMgr.size() == 0) {
                LOG() << "Warning: No instruments loaded, using fallback";
                instrumentMgr.addInstrument(fix40::Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
            }
            
            // -----------------------------------------------------------------
            // 3.2 连接行情前置
            // -----------------------------------------------------------------
            moodycamel::BlockingConcurrentQueue<fix40::MarketData> mdQueue;
            std::unique_ptr<fix40::CtpMdAdapter> mdAdapter;
            std::thread mdForwarderThread;
            
            if (!simnowConfig["md_front"].empty()) {
                fix40::CtpMdConfig mdConfig;
                mdConfig.mdFront = simnowConfig["md_front"];
                mdConfig.brokerId = simnowConfig["broker_id"];
                mdConfig.userId = simnowConfig["user_id"];
                mdConfig.password = simnowConfig["password"];
                mdConfig.flowPath = simnowConfig["flow_path"];
                if (mdConfig.flowPath.empty()) {
                    mdConfig.flowPath = "./ctp_md_flow/";
                }
                
                // 创建流文件目录
                std::filesystem::create_directories(mdConfig.flowPath);
                
                LOG() << "Connecting to CTP MD: " << mdConfig.mdFront;
                mdAdapter = std::make_unique<fix40::CtpMdAdapter>(mdQueue, mdConfig);
                
                mdAdapter->setStateCallback([](fix40::MdAdapterState state, const std::string& msg) {
                    LOG() << "[CtpMd] State: " << static_cast<int>(state) << " - " << msg;
                });
                
                if (mdAdapter->start()) {
                    // 等待连接就绪
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    
                    // 订阅所有已加载的合约
                    auto allInstruments = instrumentMgr.getAllInstrumentIds();
                    if (!allInstruments.empty()) {
                        // 分批订阅，每批最多 500 个
                        const size_t batchSize = 500;
                        for (size_t i = 0; i < allInstruments.size(); i += batchSize) {
                            size_t end = std::min(i + batchSize, allInstruments.size());
                            std::vector<std::string> batch(allInstruments.begin() + i, 
                                                           allInstruments.begin() + end);
                            mdAdapter->subscribe(batch);
                            LOG() << "Subscribed " << batch.size() << " instruments (batch " 
                                  << (i / batchSize + 1) << ")";
                        }
                    }
                    
                    // 启动行情转发线程
                    mdForwarderThread = std::thread(marketDataForwarder, 
                                                     std::ref(mdQueue), 
                                                     std::ref(engine), 
                                                     std::ref(g_running));
                } else {
                    LOG() << "Warning: Failed to start CTP MD adapter";
                }
            }
#else
        // 非 CTP 模式：使用测试合约
        LOG() << "CTP disabled, using test instruments";
        instrumentMgr.addInstrument(fix40::Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
        instrumentMgr.addInstrument(fix40::Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
        instrumentMgr.addInstrument(fix40::Instrument("AAPL", "NASDAQ", "AAPL", 0.01, 1, 1.0));
        instrumentMgr.addInstrument(fix40::Instrument("TSLA", "NASDAQ", "TSLA", 0.01, 1, 1.0));
#endif

        LOG() << "Registered " << instrumentMgr.size() << " instruments";

        // =====================================================================
        // 4. 启动服务
        // =====================================================================
        app.start();
        
        fix40::FixServer server(port, numThreads, &app);
        server.start();  // 阻塞直到收到停止信号
        
        // =====================================================================
        // 5. 优雅关闭
        // =====================================================================
        g_running = false;
        
#ifdef ENABLE_CTP
            // 停止行情转发线程
            if (mdForwarderThread.joinable()) {
                mdForwarderThread.join();
            }
            
            // 停止行情适配器
            if (mdAdapter) {
                mdAdapter->stop();
            }
        }  // simnowPath scope
#endif
        
        app.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
