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
    worker_pool_ = std::make_unique<ThreadPool>(2); // Client doesn't need many workers
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

    // Setup the main timer that drives the timing wheel
    reactor_->add_timer(1000, [this](int timer_fd) {
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
    session_->set_connection(connection_); // Set the back-reference
    
    session_->start();
    session_->schedule_timer_tasks(timing_wheel_.get());

    // Start reactor in a background thread
    reactor_thread_ = std::thread([this]{ reactor_->run(); });

    reactor_->add_fd(sock, [this](int) {
        worker_pool_->enqueue([this]{
            if(connection_) connection_->handle_read();
        });
    });
    
    auto logon = create_logon_message("CLIENT", "SERVER", 1, 30);
    session_->send(logon);
    std::cout << "Logon message sent." << std::endl;

    return true;
}

void Client::disconnect() {
    if (session_) {
        auto logout = create_logout_message("CLIENT", "SERVER", 0, "User requested logout.");
        session_->send(logout);
    }
}

void Client::run_console() {
    std::cout << "Type 'logout' to disconnect." << std::endl;
    std::string line;
    while (session_ && session_->is_running() && std::getline(std::cin, line)) {
        if (line == "logout") {
            std::cout << "Logout command issued. Sending logout message..." << std::endl;
            disconnect();
            break; // Exit the input loop
        }
    }

    // Wait for the session to terminate gracefully
    if (session_) {
        while(session_->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Client::on_connection_close() {
    // This can be called from any thread, so we make it simple: just stop the reactor.
    // The main loop in run_console will then unblock and the client can exit.
    if(reactor_) reactor_->stop();
}

} // namespace fix40 