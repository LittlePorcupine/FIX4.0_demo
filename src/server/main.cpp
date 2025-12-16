/**
 * @file main.cpp
 * @brief FIX 服务端入口点
 *
 * 加载配置，创建服务端实例并启动监听。
 */

#include "server/server.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include "app/simulation_app.hpp"
#include "app/model/instrument.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>

/**
 * @brief 服务端主函数
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 退出码（0=成功，1=失败）
 *
 * 命令行参数：
 * - argv[1]: 工作线程数（可选，0 或不指定表示使用 CPU 核心数）
 * - argv[2]: 监听端口（可选，默认从配置读取）
 */
int main(int argc, char* argv[]) {
    // 忽略 SIGPIPE 信号，防止写入已关闭的 socket 时程序退出
    signal(SIGPIPE, SIG_IGN);

    try {
        // 加载配置
        std::string config_path = "config.ini";
        if (!std::filesystem::exists(config_path)) {
            // 如果在当前目录下找不到，尝试在可执行文件同级目录找
            // 这在从 build/ 目录运行时很有用
            if (argc > 0 && std::filesystem::exists(std::filesystem::path(argv[0]).parent_path() / "config.ini")) {
                config_path = std::filesystem::path(argv[0]).parent_path() / "config.ini";
            }
        }
        if (!fix40::Config::instance().load(config_path)) {
            std::cerr << "Fatal: Failed to load config file from " << config_path << std::endl;
            return 1;
        }
        LOG() << "Config loaded from " << std::filesystem::absolute(config_path).string();


        int port = fix40::Config::instance().get_int("server", "port", 9000);
        // 可通过命令行自定义工作线程数量和端口号
        int num_threads = (argc > 1) ? std::stoi(argv[1]) : fix40::Config::instance().get_int("server", "default_threads", 0);
        if (argc > 2) {
            port = std::stoi(argv[2]);
        }
        
        // 创建模拟交易应用层
        fix40::SimulationApp app;
        
        // 注册默认测试合约
        // TODO: 后续从配置文件加载合约信息
        auto& instrumentMgr = app.getInstrumentManager();
        
        // 股指期货合约
        instrumentMgr.addInstrument(fix40::Instrument("IF2601", "CFFEX", "IF", 0.2, 300, 0.12));
        instrumentMgr.addInstrument(fix40::Instrument("IC2601", "CFFEX", "IC", 0.2, 200, 0.12));
        instrumentMgr.addInstrument(fix40::Instrument("IH2601", "CFFEX", "IH", 0.2, 300, 0.12));
        
        // 测试用股票合约（简化处理，使用1:1乘数）
        fix40::Instrument aapl("AAPL", "NASDAQ", "AAPL", 0.01, 1, 1.0);
        aapl.updateLimitPrices(200.0, 100.0);  // 设置涨跌停价
        instrumentMgr.addInstrument(aapl);
        
        fix40::Instrument tsla("TSLA", "NASDAQ", "TSLA", 0.01, 1, 1.0);
        tsla.updateLimitPrices(300.0, 200.0);
        instrumentMgr.addInstrument(tsla);
        
        LOG() << "Registered " << instrumentMgr.size() << " instruments";
        
        // 启动撮合引擎
        app.start();
        
        fix40::FixServer server(port, num_threads, &app);
        server.start();
        
        // 服务器停止后，停止撮合引擎
        app.stop();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
