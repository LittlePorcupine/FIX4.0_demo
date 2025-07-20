#include "server.hpp"
#include <iostream>
#include <csignal>

int main(int argc, char* argv[]) {
    // 全局忽略 SIGPIPE 信号
    // 这是 Unix 系统网络应用的通常做法
    // 当向已关闭的端口写数据时，避免程序立即结束，
    // 此时 send() 会返回 -1 带 errno=EPIPE
    signal(SIGPIPE, SIG_IGN);

    try {
        // 可以通过命令行自定义工作线数，
        // 否则默认为硬件核数
        int num_threads = (argc > 1) ? std::stoi(argv[1]) : 0;

        fix40::FixServer server(9000, num_threads);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
