#include "server.hpp"

#include <iostream>
#include <csignal>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "core/reactor.hpp"
#include "base/thread_pool.hpp"
#include "base/timing_wheel.hpp"
#include "core/connection.hpp"
#include "fix/session.hpp"

namespace fix40 {

// Initialize the static pointer.
FixServer* FixServer::instance_for_signal_ = nullptr;

void FixServer::signal_handler(int signum) {
    std::cout << "\nCaught signal " << signum << ". Shutting down gracefully..." << std::endl;
    if (instance_for_signal_ && instance_for_signal_->reactor_) {
        instance_for_signal_->reactor_->stop();
    }
}

FixServer::FixServer(int port, int num_threads)
    : port_(port), listen_fd_(-1) {

    worker_pool_ = std::make_unique<ThreadPool>(num_threads > 0 ? num_threads : std::thread::hardware_concurrency());
    reactor_ = std::make_unique<Reactor>();
    timing_wheel_ = std::make_unique<TimingWheel>(60, 1000); // 60 slots, 1s interval

    instance_for_signal_ = this;

    // Setup the main timer that drives the timing wheel
    reactor_->add_timer(1000, [this]([[maybe_unused]] int timer_fd) {
#ifdef __linux__
        // On Linux, timerfd needs to be drained
        uint64_t expirations;
        read(timer_fd, &expirations, sizeof(expirations));
#endif
        this->timing_wheel_->tick();
    });

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("Socket creation failed");

    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed");
    }

    if (listen(listen_fd_, 10) < 0) {
        throw std::runtime_error("Listen failed");
    }

    std::cout << "Server listening on port " << port_ << std::endl;
    std::cout << "Worker thread pool size: " << worker_pool_->get_thread_count() << std::endl;
}

FixServer::~FixServer() {
    // The unique_ptrs will handle deletion, but ensure a clean shutdown.
    if (reactor_ && reactor_->is_running()) {
        reactor_->stop();
    }
    close(listen_fd_);
    instance_for_signal_ = nullptr;
    std::cout << "FixServer destroyed." << std::endl;
}

void FixServer::start() {
    signal(SIGINT, FixServer::signal_handler);
    signal(SIGTERM, FixServer::signal_handler);

    // Add server socket to reactor to accept new connections
    reactor_->add_fd(listen_fd_, [this](int) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Accept failed" << std::endl;
            }
            return;
        }
        on_new_connection(client_fd);
    });

    reactor_->run(); // This will block until stop() is called

    // --- Graceful Shutdown Logic ---
    std::cout << "Reactor stopped. Closing listener and shutting down sessions..." << std::endl;
    reactor_->remove_fd(listen_fd_);

    // Safely get a list of connections to shutdown
    std::vector<std::shared_ptr<Connection>> conns_to_shutdown;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto const& [fd, conn] : connections_) {
            conns_to_shutdown.push_back(conn);
        }
    }

    // Now, shutdown the sessions without holding the lock
    for (const auto& conn : conns_to_shutdown) {
        conn->session()->on_shutdown("Server is shutting down");
    }

    // A simple wait for connections to close. A more robust server might have a timeout.
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connections_.empty()) {
            break;
        }
    }

    std::cout << "All sessions closed." << std::endl;
    std::cout << "Server shut down gracefully." << std::endl;
}

void FixServer::on_new_connection(int fd) {
    fcntl(fd, F_SETFL, O_NONBLOCK);
    std::cout << "Accepted new connection with fd: " << fd << std::endl;

    auto on_conn_close = [this, fd]() {
        worker_pool_->enqueue([this, fd] {
            this->on_connection_close(fd);
        });
    };

    // Create the session and connection, then link them together.
    auto session = std::make_shared<Session>("SERVER", "CLIENT", 30, on_conn_close);
    auto connection = std::make_shared<Connection>(fd, reactor_.get(), session);
    session->set_connection(connection); // Set the back-reference

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[fd] = connection;
    }

    session->start();
    session->schedule_timer_tasks(timing_wheel_.get());

    reactor_->add_fd(fd, [connection](int) {
        connection->handle_read();
    });
}

void FixServer::on_connection_close(int fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        it->second->shutdown();
        connections_.erase(it);
        std::cout << "Cleaned up resources for fd: " << fd << std::endl;
    }
}
} // namespace fix40
