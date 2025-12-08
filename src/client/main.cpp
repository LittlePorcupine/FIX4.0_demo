/**
 * @file main.cpp
 * @brief FIX 客户端入口点
 *
 * 加载配置，创建客户端实例，连接服务器并运行控制台交互。
 */

#include "client/client.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>

/**
 * @brief 客户端主函数
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 退出码（0=成功，1=失败）
 *
 * 命令行参数：
 * - argv[1]: 服务器 IP（可选，默认从配置读取）
 * - argv[2]: 服务器端口（可选，默认从配置读取）
 */
int main(int argc, char* argv[]) {
    // 忽略 SIGPIPE 信号，防止写入已关闭的 socket 时程序退出
    signal(SIGPIPE, SIG_IGN);

    try {
        // 加载配置
        std::string config_path = "config.ini";
        if (!std::filesystem::exists(config_path)) {
            if (argc > 0 && std::filesystem::exists(std::filesystem::path(argv[0]).parent_path() / "config.ini")) {
                config_path = std::filesystem::path(argv[0]).parent_path() / "config.ini";
            }
        }
        if (!fix40::Config::instance().load(config_path)) {
            std::cerr << "Fatal: Failed to load config file from " << config_path << std::endl;
            return 1;
        }
        LOG() << "Config loaded from " << std::filesystem::absolute(config_path).string();

        auto& config = fix40::Config::instance();
        std::string ip = config.get("client", "server_ip", "127.0.0.1");
        int port = config.get_int("client", "server_port", 9000);

        if (argc > 1) {
            ip = argv[1];
        }
        if (argc > 2) {
            port = std::stoi(argv[2]);
        }
        
        LOG() << "Connecting to " << ip << ":" << port << "...";

        fix40::Client client;
        if (client.connect(ip, port)) {
            client.run_console();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error in client: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
