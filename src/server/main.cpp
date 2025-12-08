/**
 * @file main.cpp
 * @brief FIX 服务端入口点
 *
 * 加载配置，创建服务端实例并启动监听。
 */

#include "server/server.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
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
        
        fix40::FixServer server(port, num_threads);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
