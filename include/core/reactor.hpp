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

// --- Platform-specific includes for I/O multiplexing ---
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif __APPLE__
#include <sys/event.h>
#include <sys/time.h> // For timer
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
    int io_fd_; // Generic fd for epoll or kqueue
    int pipe_fd_[2]; // Pipe for shutdown signal
    std::atomic<bool> running_;
    std::unordered_map<int, FdCallback> callbacks_;
    std::unordered_map<int, FdCallback> write_callbacks_; // 3. 新增写回调 map
    std::mutex mutex_;
    std::vector<int> timer_fds_;
};

// --- Implementation ---

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

    // Add pipe's read end to be monitored
    add_fd(pipe_fd_[0], nullptr); // No callback needed, just for wakeup
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
    event.events = EPOLLIN | EPOLLET; // Default to read, edge-triggered
    event.data.fd = fd;
    if (epoll_ctl(io_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl(ADD) failed");
        return false;
    }
#elif __APPLE__
    struct kevent change_event;
    // Default to read, edge-triggered (EV_CLEAR)
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
    event.events = EPOLLET; // Always use Edge-Triggered
    if (event_mask & EventType::READ) {
        event.events |= EPOLLIN;
    }
    if (event_mask & EventType::WRITE) {
        event.events |= EPOLLOUT;
    }

    if (epoll_ctl(io_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        // ENOENT means the fd is not in epoll, maybe it was closed.
        // In this case, MOD fails. It's not a fatal error for this logic.
        if (errno != ENOENT) {
            perror("epoll_ctl(MOD) failed");
            return false;
        }
    }
#elif __APPLE__
    // kqueue manages read/write filters separately. We add/delete them as needed.
    // NOTE: This assumes we always have a read callback and only toggle write.
    // A more robust implementation might track registered filters per-fd.
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
    // Track the timer fd for cleanup
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
        // ENOENT means fd is not registered, not a critical error for removal
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
        return; // Already stopping
    }
    // Write to the pipe to unblock the epoll_wait/kevent call
    char buf = 0;
    if (write(pipe_fd_[1], &buf, 1) < 0) {
        // Log error, but continue shutdown
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
            // Translate kqueue filter to our event type
            uint32_t active_events = 0;
            if (events[i].filter == EVFILT_READ) {
                active_events |= EventType::READ;
            } else if (events[i].filter == EVFILT_WRITE) {
                active_events |= EventType::WRITE;
            }

            if (events[i].filter == EVFILT_TIMER) {
                // Special handling for timer, treat as read-like event for dispatch
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
            // EPOLLERR/HUP might be set, handle them. For now, we check IN/OUT.
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
                    // Timer fds are negative
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
} // namespace fix40
