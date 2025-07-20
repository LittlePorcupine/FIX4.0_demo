#include "client.hpp"
#include <iostream>
#include <csignal>

int main() {
    // 全局忽略 SIGPIPE 信号
    // 这是 Unix 系统网络应用常用的做法
    signal(SIGPIPE, SIG_IGN);

    try {
        fix40::Client client;
        if (client.connect("127.0.0.1", 9000)) {
            client.run_console();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error in client: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
