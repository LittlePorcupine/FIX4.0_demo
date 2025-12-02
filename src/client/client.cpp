#include "client/client.hpp"
#include "core/reactor.hpp"
#include "base/thread_pool.hpp"
#include "base/timing_wheel.hpp"
#include "base/config.hpp"
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
    auto& config = Config::instance();
    worker_pool_ = std::make_unique<ThreadPool>(config.get_int("client", "worker_threads", 2));
    reactor_ = std::make_unique<Reactor>();
    timing_wheel_ = std::make_unique<TimingWheel>(
        config.get_int("timing_wheel", "slots", 60),
        config.get_int("timing_wheel", "tick_interval_ms", 1000)
    );
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

    auto& config = Config::instance();

    // 设置主定时器来驱动时钟轮
    reactor_->add_timer(config.get_int("timing_wheel", "tick_interval_ms", 1000), [this]([[maybe_unused]] int timer_fd) {
#ifdef __linux__
        uint64_t expirations;
        read(timer_fd, &expirations, sizeof(expirations));
#endif
        timing_wheel_->tick();
    });

    auto close_cb = [this]() {
        on_connection_close();
    };

    // 客户端只有一个连接，绑定到线程 0
    const size_t thread_index = 0;

    session_ = std::make_shared<Session>(
        config.get("client", "sender_comp_id", "CLIENT"),
        config.get("client", "target_comp_id", "SERVER"),
        config.get_int("fix_session", "default_heartbeat_interval", 30),
        close_cb
    );
    connection_ = std::make_shared<Connection>(
        sock, reactor_.get(), session_,
        worker_pool_.get(), thread_index
    );
    session_->set_connection(connection_);

    session_->start();
    session_->schedule_timer_tasks(timing_wheel_.get());

    // 先注册 fd，再启动 reactor 线程
    // Reactor 检测到事件后派发到绑定的工作线程处理
    std::weak_ptr<Connection> weak_conn = connection_;
    reactor_->add_fd(sock, [weak_conn](int) {
        if (auto conn = weak_conn.lock()) {
            conn->dispatch([conn]() {
                conn->handle_read();
            });
        }
    });

    // 在后台线程启动 reactor
    reactor_thread_ = std::thread([this]{ reactor_->run(); });

    // Logon is now sent from within session->start()
    // auto logon = create_logon_message("CLIENT", "SERVER", 1, 30);
    // session_->send(logon);
    // std::cout << "Logon message sent." << std::endl;

    return true;
}

void Client::disconnect() {
    // 通过 dispatch 派发到绑定的工作线程执行
    // 避免主线程直接调用 Session 方法
    if (connection_) {
        connection_->dispatch([this]() {
            if (session_) {
                session_->initiate_logout("User requested logout.");
            }
        });
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
