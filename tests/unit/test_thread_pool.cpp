#include "../catch2/catch.hpp"
#include "base/thread_pool.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <set>

using namespace fix40;

TEST_CASE("ThreadPool basic task execution", "[thread_pool]") {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    
    auto future = pool.enqueue([&counter]() {
        counter++;
        return 42;
    });
    
    REQUIRE(future.get() == 42);
    REQUIRE(counter == 1);
}

TEST_CASE("ThreadPool multiple tasks", "[thread_pool]") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int num_tasks = 100;
    
    std::vector<std::future<void>> futures;
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool.enqueue([&counter]() {
            counter++;
        }));
    }
    
    // 等待所有任务完成
    for (auto& f : futures) {
        f.get();
    }
    
    REQUIRE(counter == num_tasks);
}

TEST_CASE("ThreadPool enqueue_to specific thread", "[thread_pool]") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    
    // 向特定线程提交任务
    for (int i = 0; i < 10; ++i) {
        pool.enqueue_to(0, [&counter]() {
            counter++;
        });
    }
    
    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    REQUIRE(counter == 10);
}

TEST_CASE("ThreadPool enqueue_to with index overflow", "[thread_pool]") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    
    // 线程索引超出范围，应该取模
    pool.enqueue_to(100, [&counter]() {
        counter++;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    REQUIRE(counter == 1);
}

TEST_CASE("ThreadPool tasks on same thread execute serially", "[thread_pool]") {
    ThreadPool pool(4);
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    // 向同一个线程提交多个任务
    for (int i = 0; i < 5; ++i) {
        pool.enqueue_to(0, [i, &execution_order, &order_mutex]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(i);
        });
    }
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 同一线程的任务应该按顺序执行
    REQUIRE(execution_order.size() == 5);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(execution_order[i] == i);
    }
}

TEST_CASE("ThreadPool get_thread_count", "[thread_pool]") {
    ThreadPool pool1(1);
    REQUIRE(pool1.get_thread_count() == 1);
    
    ThreadPool pool4(4);
    REQUIRE(pool4.get_thread_count() == 4);
    
    ThreadPool pool8(8);
    REQUIRE(pool8.get_thread_count() == 8);
}

TEST_CASE("ThreadPool concurrent access safety", "[thread_pool]") {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int num_tasks = 1000;
    
    // 从多个线程同时提交任务
    std::vector<std::thread> submitters;
    for (int t = 0; t < 4; ++t) {
        submitters.emplace_back([&pool, &counter, num_tasks]() {
            for (int i = 0; i < num_tasks / 4; ++i) {
                pool.enqueue([&counter]() {
                    counter++;
                });
            }
        });
    }
    
    for (auto& t : submitters) {
        t.join();
    }
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    REQUIRE(counter == num_tasks);
}

TEST_CASE("ThreadPool task with return value", "[thread_pool]") {
    ThreadPool pool(2);
    
    auto future1 = pool.enqueue([]() { return 10; });
    auto future2 = pool.enqueue([]() { return std::string("hello"); });
    auto future3 = pool.enqueue([]() { return 3.14; });
    
    REQUIRE(future1.get() == 10);
    REQUIRE(future2.get() == "hello");
    REQUIRE(future3.get() == Approx(3.14));
}

TEST_CASE("ThreadPool task with arguments", "[thread_pool]") {
    ThreadPool pool(2);
    
    auto future = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    REQUIRE(future.get() == 30);
}

TEST_CASE("ThreadPool graceful shutdown", "[thread_pool]") {
    std::atomic<int> counter{0};
    
    {
        ThreadPool pool(2);
        
        // 提交一些任务
        for (int i = 0; i < 10; ++i) {
            pool.enqueue_to(0, [&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                counter++;
            });
        }
        
        // pool 析构时应该等待所有任务完成
    }
    
    // 析构后，所有已提交的任务应该已执行
    // 注意：由于使用 enqueue_to，任务是串行的
    REQUIRE(counter == 10);
}
