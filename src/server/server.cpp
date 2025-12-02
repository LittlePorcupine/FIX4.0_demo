#include "server/server.hpp"
#include "base/config.hpp"
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

// 初始化静态指针
FixServer* FixServer::instance_for_signal_ = nullptr;

void FixServer::signal_handler(int signum) {
    std::cout << "\nCaught signal " << signum << ". Shutting down gracefully..." << std::endl;
    if (instance_for_signal_ && instance_for_signal_->reactor_) {
        instance_for_signal_->reactor_->stop();
    }
}

FixServer::FixServer(int port, int num_threads)
    : port_(port), listen_fd_(-1) {

    auto& config = Config::instance();
    worker_pool_ = std::make_unique<ThreadPool>(num_threads > 0 ? num_threads : std::thread::hardware_concurrency());
    reactor_ = std::make_unique<Reactor>();
    timing_wheel_ = std::make_unique<TimingWheel>(
        config.get_int("timing_wheel", "slots", 60),
        config.get_int("timing_wheel", "tick_interval_ms", 1000)
    );

    instance_for_signal_ = this;

    // 设置驱动时钟轮的主定时器
    reactor_->add_timer(config.get_int("timing_wheel", "tick_interval_ms", 1000), [this]([[maybe_unused]] int timer_fd) {
#ifdef __linux__
        // Linux 上需要清空 timerfd
        uint64_t expirations;
        read(timer_fd, &expirations, sizeof(expirations));
#endif
        this->timing_wheel_->tick();
    });

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("Socket creation failed");

    // 设置 SO_REUSEADDR 选项
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }

    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed");
    }

    if (listen(listen_fd_, config.get_int("server", "listen_backlog", 128)) < 0) {
        throw std::runtime_error("Listen failed");
    }

    std::cout << "Server listening on port " << port_ << std::endl;
    std::cout << "Worker thread pool size: " << worker_pool_->get_thread_count() << std::endl;
}

FixServer::~FixServer() {
    // unique_ptr 会自动释放资源，但仍需确保干净关闭
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

    // 将服务器端口 fd 添入 reactor 以接受新连接
    reactor_->add_fd(listen_fd_, [this](int) {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd < 0) {
                // 在 ET 模式下，我们需要读到 EAGAIN 或 EWOULDBLOCK
                // 这表示当前没有更多的连接可接受
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                // 其他错误则需要记录
                std::cerr << "Accept failed with error: " << strerror(errno) << std::endl;
                break;
            }
            on_new_connection(client_fd);
        }
    });

    reactor_->run(); // 此调用会阻塞直到调用 stop()

    // --- 优雅关闭逻辑 ---
    std::cout << "Reactor stopped. Closing listener and shutting down sessions..." << std::endl;
    reactor_->remove_fd(listen_fd_);

    // 安全地获取将要关闭的连接
    std::vector<std::shared_ptr<Connection>> conns_to_shutdown;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto const& [fd, conn] : connections_) {
            conns_to_shutdown.push_back(conn);
        }
    }

    // 现在不持锁关闭会话
    for (const auto& conn : conns_to_shutdown) {
        conn->session()->on_shutdown("Server is shutting down");
    }

    // 简单地等待连接关闭，更健壮的服务器可能会设置超时
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
    
    // 计算这个连接绑定到哪个工作线程
    size_t thread_index = static_cast<size_t>(fd) % worker_pool_->get_thread_count();
    std::cout << "Accepted new connection with fd: " << fd 
              << ", bindded to thread " << thread_index << std::endl;

    auto on_conn_close = [this, fd]() {
        worker_pool_->enqueue([this, fd] {
            this->on_connection_close(fd);
        });
    };

    // 创建 session 和 connection，传入线程池和绑定的线程索引
    auto session = std::make_shared<Session>("SERVER", "CLIENT", 30, on_conn_close);
    auto connection = std::make_shared<Connection>(
        fd, reactor_.get(), session,
        worker_pool_.get(), thread_index
    );
    session->set_connection(connection);

    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[fd] = connection;
    }

    session->start();
    session->schedule_timer_tasks(timing_wheel_.get());

    // Reactor 只负责检测事件，然后派发到绑定的工作线程处理
    std::weak_ptr<Connection> weak_conn = connection;
    reactor_->add_fd(fd, [weak_conn](int) {
        if (auto conn = weak_conn.lock()) {
            conn->dispatch([conn]() {
                conn->handle_read();
            });
        }
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
} // fix40 名称空间结束
