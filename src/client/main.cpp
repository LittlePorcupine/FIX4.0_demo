#include "client/client.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include <iostream>
#include <csignal>
#include <filesystem>

int main(int argc, char* argv[]) {
    // 当写入已被关闭读取端的套接字时，此设置可防止程序退出，
    // 此时 send() 将返回 -1 并设置 errno=EPIPE。
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
