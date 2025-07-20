#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>

// --- 面向各平台的 I/O 多路处理包含 ---
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif __APPLE__
#include <sys/event.h>
#include <sys/time.h> // 为了定时器
#else
#error "Unsupported platform for Reactor"
#endif

// 1. 定义事件掩码
enum EventType : uint32_t {
    READ = 1,
    WRITE = 2
};

namespace fix40 {

class Reactor {
public:
    using FdCallback = std::function<void(int)>;

    Reactor();
    ~Reactor();

    bool add_fd(int fd, FdCallback cb);
    bool modify_fd(int fd, uint32_t event_mask, FdCallback write_cb); // 2. 新增 modify_fd
    bool add_timer(int interval_ms, FdCallback cb);
    bool remove_fd(int fd);
    void run();
    void stop();
    bool is_running() const;

private:
    int io_fd_; // epoll 或 kqueue 的通用 fd
    int pipe_fd_[2]; // 用于关闭信号的管道
    std::atomic<bool> running_;
    std::unordered_map<int, FdCallback> callbacks_;
    std::unordered_map<int, FdCallback> write_callbacks_; // 3. 新增写回调 map
    std::mutex mutex_;
    std::vector<int> timer_fds_;
};

// --- 实现 ---

inline Reactor::Reactor() : running_(false) {
#ifdef __linux__
    io_fd_ = epoll_create1(0);
#elif __APPLE__
    io_fd_ = kqueue();
#endif
    if (io_fd_ == -1) throw std::runtime_error("Failed to create I/O multiplexing instance");

    if (pipe(pipe_fd_) == -1) {
        throw std::runtime_error("Failed to create pipe for reactor shutdown");
    }

    // 将管道的读端缓存到监听列表
    add_fd(pipe_fd_[0], nullptr); // 不需要回调，只为了唤醒
}

inline Reactor::~Reactor() {
    close(io_fd_);
    close(pipe_fd_[0]);
    close(pipe_fd_[1]);
    for (int tfd : timer_fds_) {
        close(tfd);
    }
}

inline bool Reactor::add_fd(int fd, FdCallback cb) {
#ifdef __linux__
    epoll_event event;
    event.events = EPOLLIN | EPOLLET; // 默认为读事件，边缘触发
    event.data.fd = fd;
    if (epoll_ctl(io_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl(ADD) failed");
        return false;
    }
#elif __APPLE__
    struct kevent change_event;
    // 默认为读事件，边缘触发（EV_CLEAR）
    EV_SET(&change_event, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(ADD) failed");
        return false;
    }
#endif
    if (cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[fd] = std::move(cb);
    }
    return true;
}

// 4. 实现 modify_fd
inline bool Reactor::modify_fd(int fd, uint32_t event_mask, FdCallback write_cb) {
#ifdef __linux__
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLET; // 一定使用边缘触发
    if (event_mask & EventType::READ) {
        event.events |= EPOLLIN;
    }
    if (event_mask & EventType::WRITE) {
        event.events |= EPOLLOUT;
    }

    if (epoll_ctl(io_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        // ENOENT 意命该 fd 不在 epoll 中，可能已经关闭
        // 这种情况下修改操作失败，但对本逻辑并非致命的
        if (errno != ENOENT) {
            perror("epoll_ctl(MOD) failed");
            return false;
        }
    }
#elif __APPLE__
    // kqueue 分别管理读/写过滤器，我们根据需要加或删
    // 注：这个实现仅依赖常常是读回调，只会切换写监听
    // 更完善的实现可能需要记录每个 fd 的注册过滤器
    struct kevent change;
    if (event_mask & EventType::WRITE) {
        EV_SET(&change, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    } else {
        EV_SET(&change, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }

    if (kevent(io_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(MOD-WRITE) failed");
        return false;
    }
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    if (event_mask & EventType::WRITE) {
        write_callbacks_[fd] = std::move(write_cb);
    } else {
        write_callbacks_.erase(fd);
    }
    return true;
}


inline bool Reactor::add_timer(int interval_ms, FdCallback cb) {
#ifdef __linux__
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | CLOCK_REALTIME);
    if (tfd == -1) {
        perror("timerfd_create failed");
        return false;
    }
    itimerspec ts;
    ts.it_value.tv_sec = interval_ms / 1000;
    ts.it_value.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    ts.it_interval = ts.it_value;
    if (timerfd_settime(tfd, 0, &ts, nullptr) == -1) {
        perror("timerfd_settime failed");
        close(tfd);
        return false;
    }
    // 记录这个定时器 fd 以供清理
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timer_fds_.push_back(tfd);
    }
    return add_fd(tfd, std::move(cb));
#elif __APPLE__
    struct kevent change_event;
    EV_SET(&change_event, interval_ms, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, interval_ms, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(ADD TIMER) failed");
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_[-interval_ms] = std::move(cb);
    return true;
#endif
}

inline bool Reactor::remove_fd(int fd) {
#ifdef __linux__
    if (epoll_ctl(io_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl(DEL) failed");
        return false;
    }
#elif __APPLE__
    struct kevent change_event;
    EV_SET(&change_event, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(DEL) failed");
        // ENOENT 意命 fd 没有注册，尚不是致命错误
        if(errno != ENOENT) return false;
    }
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(fd);
    write_callbacks_.erase(fd); // 5. 在移除 fd 时也要清理写回调
    return true;
}

inline void Reactor::stop() {
    if (!running_.exchange(false)) {
        return; // 已经在停止
    }
    // 向管道写入以解防 epoll_wait/kevent 阻塞
    char buf = 0;
    if (write(pipe_fd_[1], &buf, 1) < 0) {
        // 记录错误，但继续关闭
        perror("Failed to write to reactor shutdown pipe");
    }
}

inline bool Reactor::is_running() const {
    return running_.load(std::memory_order_acquire);
}

inline void Reactor::run() {
    running_.store(true, std::memory_order_release);
#ifdef __linux__
    std::vector<epoll_event> events(128);
#elif __APPLE__
    std::vector<struct kevent> events(128);
#endif

    while (running_.load(std::memory_order_acquire)) {
#ifdef __linux__
        int n_events = epoll_wait(io_fd_, events.data(), events.size(), -1);
#elif __APPLE__
        int n_events = kevent(io_fd_, nullptr, 0, events.data(), events.size(), nullptr);
#endif
        if (n_events < 0) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait/kevent failed: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < n_events; ++i) {
#ifdef __linux__
            int fd = events[i].data.fd;
            uint32_t active_events = events[i].events;
#elif __APPLE__
            int fd = events[i].ident;
            // 将 kqueue 过滤类型转为本地的事件类型
            uint32_t active_events = 0;
            if (events[i].filter == EVFILT_READ) {
                active_events |= EventType::READ;
            } else if (events[i].filter == EVFILT_WRITE) {
                active_events |= EventType::WRITE;
            }

            if (events[i].filter == EVFILT_TIMER) {
                // 对定时器特殊处理，将其作为读类事件统一发送
                fd = -static_cast<int>(events[i].ident);
                active_events |= EventType::READ;
            }
#endif
            if (fd == pipe_fd_[0]) {
                char buf[1];
                read(pipe_fd_[0], buf, 1);
                continue;
            }

            // 6. 根据事件类型分发回调
#ifdef __linux__
            // EPOLLERR/HUP 可能设置，此处使用读/写事件检查
            if (active_events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
                FdCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = callbacks_.find(fd);
                    if (it != callbacks_.end()) cb = it->second;
                }
                if (cb) cb(fd);
            }
            if (active_events & EPOLLOUT) {
                FdCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = write_callbacks_.find(fd);
                    if (it != write_callbacks_.end()) cb = it->second;
                }
                if (cb) cb(fd);
            }
#elif __APPLE__
            if (active_events & EventType::READ) {
                 FdCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    // 定时器 fd 是负数
                    auto it = callbacks_.find(fd);
                    if (it != callbacks_.end()) cb = it->second;
                }
                if (cb) cb(fd);
            }
            if (active_events & EventType::WRITE) {
                 FdCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = write_callbacks_.find(fd);
                    if (it != write_callbacks_.end()) cb = it->second;
                }
                if (cb) cb(fd);
            }
#endif
        }
    }
}
} // fix40 名称空间结束
