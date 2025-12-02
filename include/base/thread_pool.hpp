#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <future>
#include <type_traits>
#include <atomic>

#include "base/blockingconcurrentqueue.h"

namespace fix40 {

/**
 * 支持"连接绑定线程"的线程池
 * 
 * 每个工作线程有独立的任务队列，可以指定任务在哪个线程执行。
 * 这样同一个连接的所有操作都在同一个线程中串行执行，避免锁竞争。
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    // 提交任务到指定线程（用于连接绑定场景）
    void enqueue_to(size_t thread_index, std::function<void()> task);

    // 提交任务到任意空闲线程（用于非连接相关的任务）
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    size_t get_thread_count() const { return thread_count_; }

private:
    std::vector<std::thread> workers_;
    // 每个线程一个独立的任务队列
    std::vector<std::unique_ptr<moodycamel::BlockingConcurrentQueue<std::function<void()>>>> task_queues_;
    std::atomic<bool> stop_;
    size_t thread_count_;
};

// --- 实现 ---

inline ThreadPool::ThreadPool(size_t threads) : stop_(false), thread_count_(threads) {
    // 为每个线程创建独立的任务队列
    for (size_t i = 0; i < threads; ++i) {
        task_queues_.push_back(
            std::make_unique<moodycamel::BlockingConcurrentQueue<std::function<void()>>>()
        );
    }

    // 创建工作线程，每个线程只从自己的队列取任务
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this, i] {
            auto& my_queue = *task_queues_[i];
            while (true) {
                std::function<void()> task;
                my_queue.wait_dequeue(task);
                if (!task) break;  // 空任务表示退出
                task();
            }
        });
    }
}

inline void ThreadPool::enqueue_to(size_t thread_index, std::function<void()> task) {
    if (stop_) return;
    if (thread_index >= thread_count_) {
        thread_index = thread_index % thread_count_;
    }
    task_queues_[thread_index]->enqueue(std::move(task));
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();

    if (stop_) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }

    // 轮询分配到各个线程（简单的负载均衡）
    static std::atomic<size_t> next_thread{0};
    size_t thread_index = next_thread.fetch_add(1) % thread_count_;
    task_queues_[thread_index]->enqueue([task]() { (*task)(); });

    return res;
}

inline ThreadPool::~ThreadPool() {
    stop_ = true;
    // 向每个线程的队列发送空任务，唤醒并退出
    for (size_t i = 0; i < thread_count_; ++i) {
        task_queues_[i]->enqueue(nullptr);
    }
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace fix40
