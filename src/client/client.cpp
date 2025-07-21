#include "client.hpp"
#include "core/reactor.hpp"
#include "base/thread_pool.hpp"
#include "base/timing_wheel.hpp"
#include "core/connection.hpp"
#include "fix/session.hpp"
#include "fix/fix_messages.hpp"

#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace fix40 {

Client::Client() {
    worker_pool_ = std::make_unique<ThreadPool>(2); // 客户端不需要很多工作线
    reactor_ = std::make_unique<Reactor>();
    timing_wheel_ = std::make_unique<TimingWheel>(60, 1000);
}

Client::~Client() {
    if (reactor_ && reactor_->is_running()) {
        reactor_->stop();
    }
    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }
    std::cout << "Client destroyed." << std::endl;
}

bool Client::connect(const std::string& ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation error" << std::endl;
        return false;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address" << std::endl;
        close(sock);
        return false;
    }
    if (::connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        close(sock);
        return false;
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);
    std::cout << "Connected to server." << std::endl;

    // 设置主定时器来驱动时钟轮
    reactor_->add_timer(1000, [this]([[maybe_unused]] int timer_fd) {
#ifdef __linux__
        uint64_t expirations;
        read(timer_fd, &expirations, sizeof(expirations));
#endif
        timing_wheel_->tick();
    });

    auto close_cb = [this]() {
        on_connection_close();
    };

    session_ = std::make_shared<Session>("CLIENT", "SERVER", 30, close_cb);
    connection_ = std::make_shared<Connection>(sock, reactor_.get(), session_);
    session_->set_connection(connection_); // 设置反向引用

    session_->start();
    session_->schedule_timer_tasks(timing_wheel_.get());

    // 在后台线程启动 reactor
    reactor_thread_ = std::thread([this]{ reactor_->run(); });

    reactor_->add_fd(sock, [this](int) {
        worker_pool_->enqueue([this]{
            if(connection_) connection_->handle_read();
        });
    });

    // Logon is now sent from within session->start()
    // auto logon = create_logon_message("CLIENT", "SERVER", 1, 30);
    // session_->send(logon);
    // std::cout << "Logon message sent." << std::endl;

    return true;
}

void Client::disconnect() {
    if (session_) {
        session_->initiate_logout("User requested logout.");
    }
}

void Client::run_console() {
    std::cout << "Type 'logout' to disconnect." << std::endl;
    std::string line;
    while (session_ && session_->is_running() && std::getline(std::cin, line)) {
        if (line == "logout") {
            std::cout << "Logout command issued. Sending logout message..." << std::endl;
            disconnect();
            break; // 退出输入循环
        }
    }

    // 等待会话平稳结束
    if (session_) {
        while(session_->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Client::on_connection_close() {
    // 可由任何线程调用，简单处理：直接停止 reactor
    // run_console 中的主循环将被解除阻塞，客户端就可退出
    if(reactor_) reactor_->stop();
}
} // fix40 名称空间结束
