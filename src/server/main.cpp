#include "server/server.hpp"
#include "base/config.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>

int main(int argc, char* argv[]) {
    // 全局忽略 SIGPIPE 信号
    // 这是 Unix 系统网络应用的通常做法
    // 当向已关闭的端口写数据时，避免程序立即结束，
    // 此时 send() 会返回 -1 带 errno=EPIPE
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
        std::cout << "Config loaded from " << std::filesystem::absolute(config_path) << std::endl;


        int port = fix40::Config::instance().get_int("server", "port", 9000);
        // 可通过命令行自定义工作线程数量和端口号
        int num_threads = (argc > 1) ? std::stoi(argv[1]) : fix40::Config::instance().get_int("server", "default_threads", 0);
        if (argc > 2) {
            port = std::stoi(argv[2]);
        }
        
        fix40::FixServer server(port, num_threads);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
