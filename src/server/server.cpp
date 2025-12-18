/**
 * @file server.cpp
 * @brief FIX 服务端实现
 */

#include "server/server.hpp"
#include "base/config.hpp"
#include "base/logger.hpp"
#include <iostream>
#include <csignal>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cerrno>

#include "core/reactor.hpp"
#include "base/thread_pool.hpp"
#include "base/timing_wheel.hpp"
#include "core/connection.hpp"
#include "fix/session.hpp"
#include "fix/application.hpp"
#include "app/simulation_app.hpp"

namespace fix40 {

// 初始化静态指针
FixServer* FixServer::instance_for_signal_ = nullptr;
volatile std::sig_atomic_t FixServer::last_signal_ = 0;

void FixServer::signal_handler(int signum) {
    // async-signal-safe: 只做最小操作（记录信号 + write self-pipe）
    last_signal_ = signum;
    if (instance_for_signal_ && instance_for_signal_->signal_pipe_[1] != -1) {
        uint8_t b = 1;
        ssize_t n = ::write(instance_for_signal_->signal_pipe_[1], &b, sizeof(b));
        (void)n;
    }
}

FixServer::FixServer(int port, int num_threads, Application* app)
    : port_(port), listen_fd_(-1), application_(app) {

    auto& config = Config::instance();
    worker_pool_ = std::make_unique<ThreadPool>(num_threads > 0 ? num_threads : std::thread::hardware_concurrency());
    reactor_ = std::make_unique<Reactor>();
    timing_wheel_ = std::make_unique<TimingWheel>(
        config.get_int("timing_wheel", "slots", 60),
        config.get_int("timing_wheel", "tick_interval_ms", 1000)
    );

    instance_for_signal_ = this;

    // self-pipe: 用于将 SIGINT/SIGTERM 从信号处理器安全地转发到 Reactor 线程
    if (pipe(signal_pipe_) != 0) {
        throw std::runtime_error("Failed to create signal pipe");
    }
    fcntl(signal_pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(signal_pipe_[1], F_SETFL, O_NONBLOCK);

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

    LOG() << "Server listening on port " << port_;
    LOG() << "Worker thread pool size: " << worker_pool_->get_thread_count();
}

FixServer::~FixServer() {
    // unique_ptr 会自动释放资源，但仍需确保干净关闭
    if (reactor_ && reactor_->is_running()) {
        reactor_->stop();
    }
    if (signal_pipe_[0] != -1) close(signal_pipe_[0]);
    if (signal_pipe_[1] != -1) close(signal_pipe_[1]);
    close(listen_fd_);
    instance_for_signal_ = nullptr;
    LOG() << "FixServer destroyed.";
}

void FixServer::start() {
    // 使用 sigaction 安装信号处理器（避免 signal 的实现差异）
    struct sigaction sa {};
    sa.sa_handler = FixServer::signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // 在 Reactor 线程里处理信号：drain pipe -> stop reactor（线程安全）
    reactor_->add_fd(signal_pipe_[0], [this](int fd) {
        uint8_t buf[128];
        while (true) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            break;
        }

        const int signum = static_cast<int>(last_signal_);
        LOG() << "\nCaught signal " << signum << ". Shutting down gracefully...";
        if (reactor_) {
            reactor_->stop();
        }
    });

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
    LOG() << "Reactor stopped. Closing listener and shutting down sessions...";
    reactor_->remove_fd(listen_fd_);
    reactor_->remove_fd(signal_pipe_[0]);

    // 安全地获取将要关闭的连接
    std::vector<std::shared_ptr<Connection>> conns_to_shutdown;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto const& [fd, conn] : connections_) {
            conns_to_shutdown.push_back(conn);
        }
    }

    // 通过 dispatch 派发关闭任务到各连接绑定的工作线程
    // 避免 Reactor 线程直接调用 Session 方法
    for (const auto& conn : conns_to_shutdown) {
        conn->dispatch([conn]() {
            conn->session()->on_shutdown("Server is shutting down");
        });
    }

    // 简单地等待连接关闭，更健壮的服务器可能会设置超时
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connections_.empty()) {
            break;
        }
    }

    LOG() << "All sessions closed.";
    LOG() << "Server shut down gracefully.";
}

void FixServer::on_new_connection(int fd) {
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    // 计算这个连接绑定到哪个工作线程
    size_t thread_index = static_cast<size_t>(fd) % worker_pool_->get_thread_count();
    LOG() << "Accepted new connection with fd: " << fd 
          << ", bound to thread " << thread_index;

    auto on_conn_close = [this, fd, thread_index]() {
        // 使用 enqueue_to 确保关闭操作在连接绑定的线程中执行，
        // 避免与该连接的其他操作（handle_read/handle_write）产生竞态
        worker_pool_->enqueue_to(thread_index, [this, fd] {
            this->on_connection_close(fd);
        });
    };

    // 若应用层提供持久化接口，则将 store 注入 Session：
    // 这样 Session 才能持久化消息与会话序列号，支持断线重连与 ResendRequest。
    auto* store = application_ ? application_->getStore() : nullptr;

    // 创建 session 和 connection，传入线程池和绑定的线程索引
    // 服务端在收到客户端 Logon 之前并不知道真实的客户端 CompID，
    // 先用占位符初始化 TargetCompID，待 Logon 解析后再更新。
    auto session = std::make_shared<Session>("SERVER", "PENDING", 30, on_conn_close, store);
    auto connection = std::make_shared<Connection>(
        fd, reactor_.get(), session,
        worker_pool_.get(), thread_index
    );
    session->set_connection(connection);

    // 设置应用层处理器
    if (application_) {
        session->set_application(application_);

        // 如果是 SimulationApp：在 Logon 完成后再注册 Session（避免多客户端 SessionID 冲突）
        if (auto* simApp = dynamic_cast<SimulationApp*>(application_)) {
            session->set_established_callback([simApp](std::shared_ptr<Session> established) {
                simApp->getSessionManager().registerSession(std::move(established));
            });
        }
    }

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
        // 从 SessionManager 注销 Session
        if (application_) {
            if (auto* simApp = dynamic_cast<SimulationApp*>(application_)) {
                auto session = it->second->session();
                if (session) {
                    simApp->getSessionManager().unregisterSession(session->get_session_id());
                }
            }
        }
        
        it->second->shutdown();
        connections_.erase(it);
        LOG() << "Cleaned up resources for fd: " << fd;
    }
}
} // fix40 名称空间结束
