#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <future>
#include <type_traits> // 供 std::invoke_result_t 使用
#include <atomic>

#include "base/safe_queue.hpp"

namespace fix40 {

class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    size_t get_thread_count() const;

private:
    std::vector<std::thread> workers_;
    SafeQueue<std::function<void()>> tasks_;

    std::atomic<bool> stop_;
    size_t thread_count_;
};

// --- 实现 ---

inline ThreadPool::ThreadPool(size_t threads) : stop_(false), thread_count_(threads) {
    for(size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            std::function<void()> task;
            while (this->tasks_.pop(task)) {
                task();
            }
        });
    }
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();

    if(stop_) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }

    tasks_.enqueue([task](){ (*task)(); });
    return res;
}

inline size_t ThreadPool::get_thread_count() const {
    return thread_count_;
}

inline ThreadPool::~ThreadPool() {
    stop_ = true;
    tasks_.stop();
    for(std::thread &worker: workers_) {
        worker.join();
    }
}
} // fix40 名称空间结束
