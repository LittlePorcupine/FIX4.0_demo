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
#include "storage/sqlite_store.hpp"
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

// 常量定义
constexpr size_t CTP_SUBSCRIPTION_BATCH_SIZE = 500;
constexpr int CTP_TRADER_CONNECT_TIMEOUT_SEC = 15;
constexpr int CTP_INSTRUMENT_QUERY_TIMEOUT_SEC = 60;
constexpr int CTP_MD_CONNECT_WAIT_SEC = 3;

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

/**
 * @brief 添加回退测试合约
 * 
 * 当无法从 CTP 查询合约时使用
 */
void addFallbackInstruments(fix40::InstrumentManager& instrumentMgr) {
    LOG() << "Adding fallback test instruments";
    instrumentMgr.addInstrument(fix40::Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
    instrumentMgr.addInstrument(fix40::Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
    instrumentMgr.addInstrument(fix40::Instrument("IH2601", "CFFEX", "IH", 0.2, 300, 0.12));
}

#ifdef ENABLE_CTP
/**
 * @brief 行情转发线程函数
 * 
 * 从行情队列读取数据，推送到撮合引擎。
 * 使用 100ms 超时轮询，确保能够响应停止信号。
 * 
 * @note 关闭时队列中未处理的数据会被丢弃，这是可接受的行为
 */
void marketDataForwarder(
    moodycamel::BlockingConcurrentQueue<fix40::MarketData>& mdQueue,
    fix40::MatchingEngine& engine,
    std::atomic<bool>& running)
{
    LOG() << "[MarketDataForwarder] Started";
    fix40::MarketData md;
    
    while (running.load()) {
        if (mdQueue.wait_dequeue_timed(md, std::chrono::milliseconds(100))) {
            engine.submitMarketData(md);
        }
    }
    
    LOG() << "[MarketDataForwarder] Stopped";
}
#endif

} // anonymous namespace

/**
 * @brief 打印使用帮助
 */
void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  -c, --config <path>   Path to config.ini (default: ./config.ini)\n"
              << "  -s, --simnow <path>   Path to simnow.ini (default: ./simnow.ini)\n"
              << "  -p, --port <port>     Server port (overrides config file)\n"
              << "  -t, --threads <num>   Worker threads (0 = auto, overrides config)\n"
              << "  -h, --help            Show this help message\n"
              << "\nConfig file search order:\n"
              << "  1. Path specified by command line option\n"
              << "  2. Current working directory\n"
              << "  3. Executable directory\n";
}

/**
 * @brief 服务端主函数
 */
int main(int argc, char* argv[]) {
    // 忽略 SIGPIPE 信号
    signal(SIGPIPE, SIG_IGN);

    // 命令行参数
    std::string configPathArg;
    std::string simnowPathArg;
    int portArg = -1;
    int threadsArg = -1;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPathArg = argv[++i];
        } else if ((arg == "-s" || arg == "--simnow") && i + 1 < argc) {
            simnowPathArg = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            portArg = std::stoi(argv[++i]);
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            threadsArg = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    try {
        // =====================================================================
        // 1. 加载 config.ini
        // =====================================================================
        std::string configPath = configPathArg.empty() 
            ? findConfigFile("config.ini", argv[0]) 
            : configPathArg;
        if (configPath.empty() || !std::filesystem::exists(configPath)) {
            std::cerr << "Fatal: config.ini not found" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        if (!fix40::Config::instance().load(configPath)) {
            std::cerr << "Fatal: Failed to load " << configPath << std::endl;
            return 1;
        }
        LOG() << "Config loaded from " << std::filesystem::absolute(configPath).string();

        // 从配置文件读取默认值，命令行参数优先
        int port = (portArg > 0) ? portArg 
            : fix40::Config::instance().get_int("server", "port", 9000);
        int numThreads = (threadsArg >= 0) ? threadsArg 
            : fix40::Config::instance().get_int("server", "default_threads", 0);

	        // =====================================================================
	        // 2. 创建 SimulationApp
	        // =====================================================================
	        const std::string dbPath =
	            fix40::Config::instance().get("storage", "db_path", "fix_server.db");
	        std::unique_ptr<fix40::SqliteStore> store;
	        if (!dbPath.empty()) {
	            store = std::make_unique<fix40::SqliteStore>(dbPath);
	        }

	        fix40::SimulationApp app(store.get());
	        auto& instrumentMgr = app.getInstrumentManager();
	        auto& engine = app.getMatchingEngine();

#ifdef ENABLE_CTP
        // =====================================================================
        // 3. CTP 行情相关变量（声明在外层作用域）
        // =====================================================================
        moodycamel::BlockingConcurrentQueue<fix40::MarketData> mdQueue;
        std::unique_ptr<fix40::CtpMdAdapter> mdAdapter;
        std::thread mdForwarderThread;
        
        // =====================================================================
        // 4. 加载 simnow.ini 并连接 CTP
        // =====================================================================
        std::string simnowPath = simnowPathArg.empty()
            ? findConfigFile("simnow.ini", argv[0])
            : simnowPathArg;
        if (simnowPath.empty()) {
            LOG() << "Warning: simnow.ini not found, using fallback test instruments";
            addFallbackInstruments(instrumentMgr);
        } else {
            LOG() << "SimNow config loaded from " << simnowPath;
            auto simnowConfig = parseIniFile(simnowPath);
            
            // -----------------------------------------------------------------
            // 4.1 连接交易前置，查询合约列表
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
                
                std::filesystem::create_directories(traderConfig.flowPath);
                
                LOG() << "Connecting to CTP Trader: " << traderConfig.traderFront;
                fix40::CtpTraderAdapter traderAdapter(traderConfig);
                traderAdapter.setInstrumentManager(&instrumentMgr);
                
                traderAdapter.setStateCallback([](fix40::CtpTraderState state, const std::string& msg) {
                    LOG() << "[CtpTrader] State: " << static_cast<int>(state) << " - " << msg;
                });
                
                if (traderAdapter.start()) {
                    if (traderAdapter.waitForReady(CTP_TRADER_CONNECT_TIMEOUT_SEC)) {
                        LOG() << "CTP Trader connected, querying instruments...";
                        traderAdapter.queryInstruments();
                        if (traderAdapter.waitForQueryComplete(CTP_INSTRUMENT_QUERY_TIMEOUT_SEC)) {
                            LOG() << "Loaded " << instrumentMgr.size() << " instruments from CTP";
                        } else {
                            LOG() << "Warning: Instrument query timeout (loaded " 
                                  << instrumentMgr.size() << " instruments so far)";
                        }
                    } else {
                        LOG() << "Warning: CTP Trader connection timeout after " 
                              << CTP_TRADER_CONNECT_TIMEOUT_SEC << " seconds";
                    }
                    traderAdapter.stop();
                } else {
                    LOG() << "Warning: Failed to start CTP Trader adapter";
                }
            }
            
            // 如果没有查询到合约，使用回退
            if (instrumentMgr.size() == 0) {
                LOG() << "Warning: No instruments loaded from CTP";
                addFallbackInstruments(instrumentMgr);
            }
            
            // -----------------------------------------------------------------
            // 4.2 连接行情前置
            // -----------------------------------------------------------------
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
                
                std::filesystem::create_directories(mdConfig.flowPath);
                
                LOG() << "Connecting to CTP MD: " << mdConfig.mdFront;
                mdAdapter = std::make_unique<fix40::CtpMdAdapter>(mdQueue, mdConfig);
                
                mdAdapter->setStateCallback([](fix40::MdAdapterState state, const std::string& msg) {
                    LOG() << "[CtpMd] State: " << static_cast<int>(state) << " - " << msg;
                });
                
                if (mdAdapter->start()) {
                    // 等待连接就绪
                    // TODO: 改用状态回调或条件变量替代硬等待
                    std::this_thread::sleep_for(std::chrono::seconds(CTP_MD_CONNECT_WAIT_SEC));
                    
                    // 订阅所有已加载的合约
                    auto allInstruments = instrumentMgr.getAllInstrumentIds();
                    if (!allInstruments.empty()) {
                        for (size_t i = 0; i < allInstruments.size(); i += CTP_SUBSCRIPTION_BATCH_SIZE) {
                            size_t end = std::min(i + CTP_SUBSCRIPTION_BATCH_SIZE, allInstruments.size());
                            std::vector<std::string> batch(allInstruments.begin() + i, 
                                                           allInstruments.begin() + end);
                            mdAdapter->subscribe(batch);
                            LOG() << "Subscribed " << batch.size() << " instruments (batch " 
                                  << (i / CTP_SUBSCRIPTION_BATCH_SIZE + 1) << ")";
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
        }
#else
        // 非 CTP 模式：使用测试合约
        LOG() << "CTP disabled, using test instruments";
        addFallbackInstruments(instrumentMgr);
        instrumentMgr.addInstrument(fix40::Instrument("AAPL", "NASDAQ", "AAPL", 0.01, 1, 1.0));
        instrumentMgr.addInstrument(fix40::Instrument("TSLA", "NASDAQ", "TSLA", 0.01, 1, 1.0));
#endif

        LOG() << "Registered " << instrumentMgr.size() << " instruments";

        // =====================================================================
        // 5. 启动服务
        // =====================================================================
        app.start();
        
        fix40::FixServer server(port, numThreads, &app);
        server.start();  // 阻塞直到收到停止信号
        
        // =====================================================================
        // 6. 优雅关闭
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
#endif
        
        app.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
