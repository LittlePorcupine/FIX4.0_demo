/**
 * @file main.cpp
 * @brief FIX 交易客户端入口点
 *
 * 启动流程：
 * 1. 解析命令行参数
 * 2. 创建 FIX 连接
 * 3. 启动 TUI 界面
 */

#include "client_state.hpp"
#include "client_app.hpp"
#include "tui/app.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include "core/reactor.hpp"
#include "core/connection.hpp"
#include "fix/session.hpp"
#include "base/thread_pool.hpp"
#include "base/timing_wheel.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <filesystem>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

namespace {

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  -h, --host <host>     Server host (default: 127.0.0.1)\n"
              << "  -p, --port <port>     Server port (default: 9000)\n"
              << "  -u, --user <userId>   User ID / SenderCompID (required)\n"
              << "  -c, --config <path>   Path to config.ini\n"
              << "  --help                Show this help message\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // 忽略 SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    // 默认参数
    std::string host = "127.0.0.1";
    int port = 9000;
    std::string userId;
    std::string configPath;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-h" || arg == "--host") && i + 1 < argc) {
            host = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-u" || arg == "--user") && i + 1 < argc) {
            userId = argv[++i];
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // 检查必要参数
    if (userId.empty()) {
        std::cerr << "Error: User ID is required (-u/--user)\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // 加载配置文件（可选）
    if (!configPath.empty() && std::filesystem::exists(configPath)) {
        fix40::Config::instance().load(configPath);
    }
    
    try {
        // 创建客户端状态
        auto state = std::make_shared<fix40::client::ClientState>();
        state->setConnectionState(fix40::client::ConnectionState::CONNECTING);
        
        // 加载历史订单
        state->loadOrders();
        
        // 创建 FIX Application
        auto app = std::make_shared<fix40::client::ClientApp>(state, userId);
        
        // 创建网络组件
        auto reactor = std::make_unique<fix40::Reactor>();
        auto threadPool = std::make_unique<fix40::ThreadPool>(1);
        auto timingWheel = std::make_unique<fix40::TimingWheel>(60, 1000);
        
        // 设置时钟轮定时器
        reactor->add_timer(1000, [&timingWheel](int) {
            timingWheel->tick();
        });
        
        // 连接服务器
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return 1;
        }
        
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "Invalid address: " << host << std::endl;
            close(sockfd);
            return 1;
        }
        
        LOG() << "Connecting to " << host << ":" << port << "...";
        
        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
            state->setConnectionState(fix40::client::ConnectionState::ERROR);
            state->setLastError("连接失败");
            close(sockfd);
            return 1;
        }
        
        // 设置非阻塞
        fcntl(sockfd, F_SETFL, O_NONBLOCK);
        
        state->setConnectionState(fix40::client::ConnectionState::CONNECTED);
        LOG() << "Connected to server";
        
        // 创建 Session（Client 端：senderCompID=userId, targetCompID=SERVER）
        auto session = std::make_shared<fix40::Session>(
            userId, "SERVER", 30,
            [&state] {
                state->setConnectionState(fix40::client::ConnectionState::DISCONNECTED);
                state->addMessage("连接已断开");
            });
        
        // 创建 Connection
        auto connection = std::make_shared<fix40::Connection>(
            sockfd, reactor.get(), session,
            threadPool.get(), 0);
        
        session->set_connection(connection);
        session->set_application(app.get());
        app->setSession(session);
        
        // 添加到 Reactor
        std::weak_ptr<fix40::Connection> weakConn = connection;
        reactor->add_fd(sockfd, [weakConn](int) {
            if (auto conn = weakConn.lock()) {
                conn->dispatch([conn]() {
                    conn->handle_read();
                });
            }
        });
        
        // 启动 Session
        LOG() << "Starting session...";
        session->start();
        session->schedule_timer_tasks(timingWheel.get());
        state->setConnectionState(fix40::client::ConnectionState::LOGGING_IN);
        
        // 在后台线程运行 Reactor
        LOG() << "Starting reactor thread...";
        std::thread reactorThread([&reactor] {
            reactor->run();
        });
        
        // 等待一小段时间让登录完成
        LOG() << "Waiting for login...";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 创建并运行 TUI
        LOG() << "Creating TUI...";
        fix40::client::tui::TuiApp tuiApp(state, app);
        
        // 禁用日志输出，避免干扰 TUI 界面
        LOG() << "Running TUI (disabling log output)...";
        fix40::Logger::instance().setEnabled(false);
        
        tuiApp.run();
        
        // TUI 退出后恢复日志
        fix40::Logger::instance().setEnabled(true);
        
        // 保存订单
        state->saveOrders();
        
        // TUI 退出后清理
        
        // 发送 Logout
        if (state->getConnectionState() == fix40::client::ConnectionState::LOGGED_IN) {
            session->initiate_logout("Client exit");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        reactor->stop();
        if (reactorThread.joinable()) {
            reactorThread.join();
        }
        
        close(sockfd);
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
