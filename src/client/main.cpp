#include "client/client.hpp"
#include <iostream>
#include <csignal>

int main(int argc, char* argv[]) {
    // 当写入已被关闭读取端的套接字时，此设置可防止程序退出，
    // 此时 send() 将返回 -1 并设置 errno=EPIPE。
    signal(SIGPIPE, SIG_IGN);

    try {
        std::string ip = "127.0.0.1";
        int port = 9000;

        if (argc > 1) {
            ip = argv[1];
        }
        if (argc > 2) {
            port = std::stoi(argv[2]);
        }
        
        std::cout << "Connecting to " << ip << ":" << port << "..." << std::endl;

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
