/**
 * @file reactor.hpp
 * @brief Reactor 模式事件循环实现
 *
 * 提供跨平台的 I/O 多路复用封装，支持 Linux (epoll) 和 macOS (kqueue)。
 * 采用边缘触发（ET）模式，通过无锁队列实现线程安全的异步任务提交。
 */

#pragma once

#include <functional>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <unistd.h> // 为 read, write, close 提供 POSIX 函数声明

#include "base/concurrentqueue.h" // 使用 moodycamel 的无锁队列

// --- 面向各平台的 I/O 多路处理包含 ---
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h> // 使用 eventfd 来唤醒
#elif __APPLE__
#include <sys/event.h>
#include <sys/time.h>
#else
#error "Unsupported platform for Reactor"
#endif

namespace fix40 {

/**
 * @enum EventType
 * @brief I/O 事件类型
 */
enum class EventType : uint32_t {
    READ = 1,   ///< 可读事件
    WRITE = 2   ///< 可写事件
};

/**
 * @class Reactor
 * @brief 基于 Reactor 模式的事件循环
 *
 * 封装 epoll (Linux) 或 kqueue (macOS) 实现高性能 I/O 多路复用。
 *
 * @par 设计特点
 * - 边缘触发（ET）模式，减少系统调用次数
 * - 无锁任务队列，支持跨线程安全提交任务
 * - 支持文件描述符事件和定时器事件
 * - 通过 eventfd/pipe 实现跨线程唤醒
 *
 * @par 线程模型
 * - run() 方法在单线程中执行事件循环
 * - add_fd/modify_fd/remove_fd/add_timer 可从任意线程调用
 * - 实际的 fd 操作通过任务队列在事件循环线程中执行
 *
 * @par 使用示例
 * @code
 * Reactor reactor;
 * 
 * // 注册读事件
 * reactor.add_fd(client_fd, [](int fd) {
 *     char buf[1024];
 *     read(fd, buf, sizeof(buf));
 * });
 * 
 * // 添加定时器
 * reactor.add_timer(1000, [](int) {
 *     std::cout << "Timer fired!" << std::endl;
 * });
 * 
 * // 启动事件循环（阻塞）
 * reactor.run();
 * @endcode
 */
class Reactor {
public:
    /// 文件描述符事件回调类型，参数为触发事件的 fd
    using FdCallback = std::function<void(int)>;
    /// 异步任务类型
    using Task = std::function<void()>;

    /**
     * @brief 构造 Reactor
     *
     * 创建 epoll/kqueue 实例和唤醒机制（eventfd/pipe）。
     *
     * @throws std::runtime_error 创建失败时抛出异常
     */
    Reactor();

    /**
     * @brief 析构 Reactor
     *
     * 关闭 epoll/kqueue 实例和所有定时器 fd。
     */
    ~Reactor();

    /**
     * @brief 注册文件描述符的读事件
     * @param fd 文件描述符（应已设置为非阻塞）
     * @param cb 读事件回调函数
     *
     * 可从任意线程调用，实际注册在事件循环线程中执行。
     */
    void add_fd(int fd, FdCallback cb);

    /**
     * @brief 修改文件描述符的事件监听
     * @param fd 文件描述符
     * @param event_mask 事件掩码（EventType::READ | EventType::WRITE）
     * @param write_cb 写事件回调函数（可为 nullptr）
     *
     * 用于动态添加/移除写事件监听。
     */
    void modify_fd(int fd, uint32_t event_mask, FdCallback write_cb);

    /**
     * @brief 添加周期性定时器
     * @param interval_ms 定时间隔（毫秒）
     * @param cb 定时器回调函数
     *
     * @note Linux 使用 timerfd，macOS 使用 kqueue EVFILT_TIMER
     */
    void add_timer(int interval_ms, FdCallback cb);

    /**
     * @brief 移除文件描述符
     * @param fd 要移除的文件描述符
     *
     * 从 epoll/kqueue 中移除 fd 并清理回调。
     */
    void remove_fd(int fd);

    /**
     * @brief 启动事件循环
     *
     * 阻塞当前线程，持续处理 I/O 事件直到调用 stop()。
     *
     * @par 事件循环流程
     * 1. 处理任务队列中的挂起任务
     * 2. 调用 epoll_wait/kevent 等待事件
     * 3. 分发事件到对应的回调函数
     * 4. 重复以上步骤
     */
    void run();

    /**
     * @brief 停止事件循环
     *
     * 设置停止标志并唤醒事件循环，使 run() 返回。
     * 可从任意线程调用。
     */
    void stop();

    /**
     * @brief 检查事件循环是否正在运行
     * @return true 正在运行
     * @return false 已停止或未启动
     */
    bool is_running() const;

private:
    /**
     * @brief 实际执行添加 fd 操作
     * @param fd 文件描述符
     * @param cb 回调函数
     */
    void do_add_fd(int fd, FdCallback cb);

    /**
     * @brief 实际执行修改 fd 操作
     */
    void do_modify_fd(int fd, uint32_t event_mask, FdCallback write_cb);

    /**
     * @brief 实际执行添加定时器操作
     */
    void do_add_timer(int interval_ms, FdCallback cb);

    /**
     * @brief 实际执行移除 fd 操作
     */
    void do_remove_fd(int fd);

    /**
     * @brief 处理任务队列中的所有挂起任务
     */
    void process_tasks();

    /**
     * @brief 唤醒阻塞在 epoll_wait/kevent 的事件循环
     */
    void wakeup();

    int io_fd_; ///< epoll 或 kqueue 的文件描述符
#ifdef __linux__
    int wakeup_fd_; ///< Linux 使用 eventfd 唤醒
#else
    int wakeup_pipe_[2]; ///< macOS 使用 pipe 唤醒
#endif
    std::atomic<bool> running_;  ///< 运行状态标志
    std::unordered_map<int, FdCallback> callbacks_;       ///< 读事件回调映射
    std::unordered_map<int, FdCallback> write_callbacks_; ///< 写事件回调映射
    std::vector<int> timer_fds_; ///< 定时器 fd 列表（仅 Linux）

    moodycamel::ConcurrentQueue<Task> tasks_; ///< 无锁任务队列
};

// --- 实现 ---

inline Reactor::Reactor() : running_(false) {
#ifdef __linux__
    io_fd_ = epoll_create1(0);
    if (io_fd_ == -1) throw std::runtime_error("Failed to create epoll instance");
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ == -1) {
        close(io_fd_);
        throw std::runtime_error("Failed to create eventfd for wakeup");
    }
    do_add_fd(wakeup_fd_, [this](int fd){
        uint64_t one;
        ssize_t n = read(fd, &one, sizeof(one));
        if (n != sizeof(one)) {
            // 错误处理
        }
    });
#elif __APPLE__
    io_fd_ = kqueue();
    if (io_fd_ == -1) throw std::runtime_error("Failed to create kqueue instance");
    if (pipe(wakeup_pipe_) == -1) {
        close(io_fd_);
        throw std::runtime_error("Failed to create pipe for reactor wakeup");
    }
    do_add_fd(wakeup_pipe_[0], nullptr); // 不需要回调，只为了唤醒
#endif
}

inline Reactor::~Reactor() {
    close(io_fd_);
#ifdef __linux__
    close(wakeup_fd_);
    for (int tfd : timer_fds_) {
        close(tfd);
    }
#elif __APPLE__
    close(wakeup_pipe_[0]);
    close(wakeup_pipe_[1]);
#endif
}

inline void Reactor::wakeup() {
#ifdef __linux__
    uint64_t one = 1;
    ssize_t n = write(wakeup_fd_, &one, sizeof(one));
    if (n != sizeof(one)) {
         // 错误处理
    }
#elif __APPLE__
    char buf = 'w';
    if (write(wakeup_pipe_[1], &buf, 1) < 0) {
        // 错误处理
    }
#endif
}

inline void Reactor::add_fd(int fd, FdCallback cb) {
    tasks_.enqueue([this, fd, cb = std::move(cb)]() {
        do_add_fd(fd, cb);
    });
    wakeup();
}

inline void Reactor::modify_fd(int fd, uint32_t event_mask, FdCallback write_cb) {
    tasks_.enqueue([this, fd, event_mask, cb = std::move(write_cb)]() {
        do_modify_fd(fd, event_mask, cb);
    });
    wakeup();
}

inline void Reactor::add_timer(int interval_ms, FdCallback cb) {
    tasks_.enqueue([this, interval_ms, cb = std::move(cb)]() {
        do_add_timer(interval_ms, cb);
    });
    wakeup();
}

inline void Reactor::remove_fd(int fd) {
    tasks_.enqueue([this, fd]() {
        do_remove_fd(fd);
    });
    wakeup();
}

inline void Reactor::do_add_fd(int fd, FdCallback cb) {
#ifdef __linux__
    epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = fd;
    if (epoll_ctl(io_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl(ADD) failed");
        return;
    }
#elif __APPLE__
    struct kevent change_event;
    EV_SET(&change_event, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(ADD) failed");
        return;
    }
#endif
    if (cb) {
        callbacks_[fd] = std::move(cb);
    }
}

inline void Reactor::do_modify_fd(int fd, uint32_t event_mask, FdCallback write_cb) {
#ifdef __linux__
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLET;
    if (static_cast<uint32_t>(event_mask) & static_cast<uint32_t>(EventType::READ)) {
        event.events |= EPOLLIN;
    }
    if (static_cast<uint32_t>(event_mask) & static_cast<uint32_t>(EventType::WRITE)) {
        event.events |= EPOLLOUT;
    }
    if (epoll_ctl(io_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        if (errno != ENOENT) {
            perror("epoll_ctl(MOD) failed");
        }
        return;
    }
#elif __APPLE__
    struct kevent change;
    if (static_cast<uint32_t>(event_mask) & static_cast<uint32_t>(EventType::WRITE)) {
        EV_SET(&change, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    } else {
        EV_SET(&change, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    if (kevent(io_fd_, &change, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(MOD-WRITE) failed");
    }
#endif
    if (static_cast<uint32_t>(event_mask) & static_cast<uint32_t>(EventType::WRITE)) {
        write_callbacks_[fd] = std::move(write_cb);
    } else {
        write_callbacks_.erase(fd);
    }
}

inline void Reactor::do_add_timer(int interval_ms, FdCallback cb) {
#ifdef __linux__
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd == -1) {
        perror("timerfd_create failed");
        return;
    }
    itimerspec ts;
    ts.it_value.tv_sec = interval_ms / 1000;
    ts.it_value.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    ts.it_interval = ts.it_value;
    if (timerfd_settime(tfd, 0, &ts, nullptr) == -1) {
        perror("timerfd_settime failed");
        close(tfd);
        return;
    }
    timer_fds_.push_back(tfd);
    do_add_fd(tfd, std::move(cb));
#elif __APPLE__
    struct kevent change_event;
    EV_SET(&change_event, interval_ms, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, interval_ms, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        perror("kevent(ADD TIMER) failed");
        return;
    }
    callbacks_[-interval_ms] = std::move(cb);
#endif
}

inline void Reactor::do_remove_fd(int fd) {
#ifdef __linux__
    if (epoll_ctl(io_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        // ENOENT: fd 不在 epoll 中
        // EBADF: fd 已关闭（Linux 会自动从 epoll 移除已关闭的 fd）
        if (errno != ENOENT && errno != EBADF) {
            perror("epoll_ctl(DEL) failed");
        }
    }
#elif __APPLE__
    struct kevent change_event;
    EV_SET(&change_event, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1 && errno != ENOENT && errno != EBADF) {
        perror("kevent(DEL READ) failed");
    }
    EV_SET(&change_event, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    if (kevent(io_fd_, &change_event, 1, nullptr, 0, nullptr) == -1 && errno != ENOENT && errno != EBADF) {
        perror("kevent(DEL WRITE) failed");
    }
#endif
    callbacks_.erase(fd);
    write_callbacks_.erase(fd);
}


inline void Reactor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    wakeup(); // 唤醒 run() 循环以使其退出
}

inline bool Reactor::is_running() const {
    return running_.load(std::memory_order_acquire);
}

inline void Reactor::process_tasks() {
    Task task;
    while(tasks_.try_dequeue(task)) {
        task();
    }
}

inline void Reactor::run() {
    running_.store(true, std::memory_order_release);
#ifdef __linux__
    std::vector<epoll_event> events(128);
#elif __APPLE__
    std::vector<struct kevent> events(128);
#endif

    while (running_.load(std::memory_order_acquire)) {
        process_tasks(); // 每次循环前处理挂起的任务

#ifdef __linux__
        int n_events = epoll_wait(io_fd_, events.data(), events.size(), -1);
#elif __APPLE__
        int n_events = kevent(io_fd_, nullptr, 0, events.data(), events.size(), nullptr);
#endif
        if (!running_) break; // 捕获 stop() 信号后退出

        if (n_events < 0) {
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait/kevent failed: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < n_events; ++i) {
#ifdef __linux__
            int fd = events[i].data.fd;
            if (fd == wakeup_fd_) {
                uint64_t one;
                read(wakeup_fd_, &one, sizeof(one)); // 清空 eventfd
                continue;
            }
            uint32_t active_events = events[i].events;
#elif __APPLE__
            int fd = events[i].ident;
            if (fd == wakeup_pipe_[0]) {
                char buf[1];
                read(wakeup_pipe_[0], buf, 1);
                continue;
            }
            uint32_t active_events = 0;
            if (events[i].filter == EVFILT_READ) active_events |= static_cast<uint32_t>(EventType::READ);
            if (events[i].filter == EVFILT_WRITE) active_events |= static_cast<uint32_t>(EventType::WRITE);
            if (events[i].filter == EVFILT_TIMER) {
                fd = -static_cast<int>(events[i].ident);
                active_events |= static_cast<uint32_t>(EventType::READ);
            }
#endif

#ifdef __linux__
            if (active_events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end() && it->second) it->second(fd);
            }
            if (active_events & EPOLLOUT) {
                auto it = write_callbacks_.find(fd);
                if (it != write_callbacks_.end() && it->second) it->second(fd);
            }
#elif __APPLE__
            if (active_events & static_cast<uint32_t>(EventType::READ)) {
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end() && it->second) it->second(fd);
            }
            if (active_events & static_cast<uint32_t>(EventType::WRITE)) {
                auto it = write_callbacks_.find(fd);
                if (it != write_callbacks_.end() && it->second) it->second(fd);
            }
#endif
        }
    }
}
} // fix40 名称空间结束
