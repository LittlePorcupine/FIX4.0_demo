/**
 * @file thread_pool.hpp
 * @brief 支持连接绑定的线程池实现
 *
 * 提供高性能的线程池，支持将任务派发到指定线程执行，
 * 实现"连接绑定线程"模型，避免同一连接的操作产生锁竞争。
 */

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
 * @class ThreadPool
 * @brief 支持"连接绑定线程"的线程池
 *
 * 每个工作线程有独立的任务队列，可以指定任务在哪个线程执行。
 * 这样同一个连接的所有操作都在同一个线程中串行执行，避免锁竞争。
 *
 * @par 设计特点
 * - 每个线程独立的无锁任务队列（moodycamel::BlockingConcurrentQueue）
 * - 支持指定线程执行任务（enqueue_to）
 * - 支持轮询分配任务（enqueue）
 * - 优雅关闭：发送空任务通知线程退出
 *
 * @par 使用示例
 * @code
 * ThreadPool pool(4);
 * 
 * // 将任务派发到指定线程（连接绑定场景）
 * pool.enqueue_to(connection_fd % pool.get_thread_count(), [&]() {
 *     handle_connection(connection_fd);
 * });
 * 
 * // 提交任务到任意线程
 * auto future = pool.enqueue([]() { return compute_result(); });
 * @endcode
 */
class ThreadPool {
public:
    /**
     * @brief 构造线程池
     * @param threads 工作线程数量
     *
     * 创建指定数量的工作线程，每个线程有独立的任务队列。
     */
    explicit ThreadPool(size_t threads);

    /**
     * @brief 析构线程池
     *
     * 向每个线程发送空任务以通知退出，然后等待所有线程结束。
     */
    ~ThreadPool();

    /**
     * @brief 提交任务到指定线程
     * @param thread_index 目标线程索引（会自动取模）
     * @param task 要执行的任务
     *
     * 用于连接绑定场景，确保同一连接的所有操作在同一线程串行执行。
     *
     * @note 如果 thread_index >= thread_count_，会自动取模
     * @note 线程池停止后调用此方法无效
     */
    void enqueue_to(size_t thread_index, std::function<void()> task);

    /**
     * @brief 提交任务到任意空闲线程
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 调用参数
     * @return std::future 用于获取任务返回值
     *
     * 使用轮询方式分配任务到各线程，实现简单的负载均衡。
     *
     * @throws std::runtime_error 如果线程池已停止
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief 获取线程池中的线程数量
     * @return size_t 线程数量
     */
    size_t get_thread_count() const { return thread_count_; }

private:
    std::vector<std::thread> workers_; ///< 工作线程数组
    /// 每个线程独立的任务队列
    std::vector<std::unique_ptr<moodycamel::BlockingConcurrentQueue<std::function<void()>>>> task_queues_;
    std::atomic<bool> stop_;   ///< 停止标志
    size_t thread_count_;      ///< 线程数量
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
