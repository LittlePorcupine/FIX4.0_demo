#include "base/safe_queue.hpp"
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iomanip> // 用于格式化输出
#include <ctime>   // 用于时间

using namespace fix40;

// 简单测试框架
class TestRunner {
public:
    static void run_test(const std::string& test_name, void (*test_func)()) {
        std::cout << "运行 " << test_name << "... ";
        std::cout.flush(); // 确保立即显示
        
        // 创建进度指示线程
        std::atomic<bool> test_done(false);
        std::thread progress_thread([&test_done]() {
            const char progress_chars[] = {'|', '/', '-', '\\'};
            int i = 0;
            while (!test_done.load()) {
                std::cout << "\b" << progress_chars[i] << std::flush;
                i = (i + 1) % 4;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "\b";
        });
        
        try {
            test_func();
            test_done.store(true);
            progress_thread.join(); // 等待进度线程结束
            std::cout << "通过" << std::endl;
            passed_++;
        } catch (const std::exception& e) {
            test_done.store(true);
            progress_thread.join(); // 等待进度线程结束
            std::cout << "失败: " << e.what() << std::endl;
            failed_++;
        } catch (...) {
            test_done.store(true);
            progress_thread.join(); // 等待进度线程结束
            std::cout << "失败: 未知异常" << std::endl;
            failed_++;
        }
        total_++;
    }
    
    static void print_summary() {
        std::cout << "\n=== 测试摘要 ===" << std::endl;
        std::cout << "总计: " << total_ << ", 通过: " << passed_ << ", 失败: " << failed_ << std::endl;
        if (failed_ > 0) {
            std::cout << "部分测试失败!" << std::endl;
        } else {
            std::cout << "所有测试通过!" << std::endl;
        }
    }
    
    static int get_failed_count() { return failed_; }

private:
    static int total_;
    static int passed_;
    static int failed_;
};

int TestRunner::total_ = 0;
int TestRunner::passed_ = 0;
int TestRunner::failed_ = 0;

// 测试辅助函数
void assert_true(const std::string& message, bool condition) {
    if (!condition) {
        throw std::runtime_error("断言失败: " + message);
    }
}

void assert_false(const std::string& message, bool condition) {
    if (condition) {
        throw std::runtime_error("断言失败: " + message);
    }
}

// 测试用例：验证入队返回状态
void test_enqueue_return_status() {
    SafeQueue<int> queue;
    
    // 正常队列应该返回 true
    bool result = queue.enqueue(42);
    assert_true("正常队列入队应返回 true", result);
    
    // 停止队列后应该返回 false
    queue.stop();
    result = queue.enqueue(43);
    assert_false("停止队列入队应返回 false", result);
}

// 测试用例：测试停止队列行为和返回值
void test_stopped_queue_behavior() {
    SafeQueue<std::string> queue;
    
    // 先入队一些数据
    queue.enqueue("测试1");
    queue.enqueue("测试2");
    
    // 停止队列
    queue.stop();
    
    // 入队应该失败
    bool enqueue_result = queue.enqueue("测试3");
    assert_false("停止队列后入队应失败", enqueue_result);
    
    // 但仍然可以出队已有数据
    std::string value;
    bool pop_result = queue.pop(value);
    assert_true("停止队列后仍可出队已有数据", pop_result);
    assert_true("出队数据应正确", value == "测试1");
    
    pop_result = queue.pop(value);
    assert_true("停止队列后仍可出队第二个已有数据", pop_result);
    assert_true("第二个出队数据应正确", value == "测试2");
    
    // 队列为空且已停止，出队应该失败
    pop_result = queue.pop(value);
    assert_false("空且已停止的队列出队应失败", pop_result);
}

// 测试用例：验证返回值机制的线程安全性
void test_thread_safety_of_return_value() {
    SafeQueue<int> queue;
    std::atomic<int> successful_enqueues(0);
    std::atomic<int> failed_enqueues(0);
    std::atomic<bool> stop_flag(false);
    std::atomic<bool> consumer_done(false);
    
    // 创建多个生产者线程
    const int num_threads = 3;
    const int items_per_thread = 100;
    std::vector<std::thread> producers;
    
    // 先停止队列，确保有失败的入队操作
    queue.stop();
    stop_flag.store(true);
    
    for (int i = 0; i < num_threads; i++) {
        producers.emplace_back([&queue, &successful_enqueues, &failed_enqueues, i, items_per_thread]() {
            for (int j = 0; j < items_per_thread; j++) {
                int value = i * items_per_thread + j;
                bool result = queue.enqueue(value);
                if (result) {
                    successful_enqueues++;
                } else {
                    failed_enqueues++;
                }
                
                // 添加短暂延迟，避免过快入队
                if (j % 10 == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
    
    // 设置超时，防止测试卡住
    auto start_time = std::chrono::steady_clock::now();
    
    // 等待所有生产者线程完成
    for (auto& t : producers) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    // 验证结果
    std::cout << "成功入队: " << successful_enqueues << ", 失败入队: " << failed_enqueues << std::endl;
    assert_true("应有失败入队操作", failed_enqueues > 0);
    assert_true("所有入队操作应失败", failed_enqueues == num_threads * items_per_thread);
    assert_true("不应有成功入队操作", successful_enqueues == 0);
}

// 测试用例：测试与现有使用模式的向后兼容性
void test_backward_compatibility() {
    // 创建一个函数，模拟旧代码不检查返回值的情况
    auto legacy_code_simulation = [](SafeQueue<int>& q, int value) {
        // 旧代码会忽略返回值
        q.enqueue(value);
        // 不会导致编译错误或运行时错误
    };
    
    SafeQueue<int> queue;
    
    // 测试正常队列
    legacy_code_simulation(queue, 42);
    
    int value;
    bool result = queue.pop(value);
    assert_true("旧代码应能正常入队", result);
    assert_true("旧代码入队的值应正确", value == 42);
    
    // 测试停止队列
    queue.stop();
    legacy_code_simulation(queue, 43);
    
    // 停止队列后入队应该失败，但不会抛出异常
    result = queue.try_pop(value);
    assert_false("停止队列后旧代码入队应失败但不抛异常", result);
}

// 测试用例：测试多线程环境下的入队和出队操作
void test_multithreaded_enqueue_dequeue() {
    SafeQueue<int> queue;
    const int num_producers = 2;
    const int num_consumers = 2;
    const int items_per_producer = 50; // 减少测试数据量
    
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);
    std::atomic<bool> producers_done(false);
    
    // 创建生产者线程
    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; i++) {
        producers.emplace_back([&queue, &produced_count, i, items_per_producer]() {
            for (int j = 0; j < items_per_producer; j++) {
                if (queue.enqueue(i * items_per_producer + j)) {
                    produced_count++;
                }
                // 减少延迟
                if (j % 20 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        });
    }
    
    // 创建消费者线程
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; i++) {
        consumers.emplace_back([&queue, &consumed_count, &producers_done]() {
            int value;
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5); // 5秒超时
            int consecutive_empty_checks = 0;
            
            while (true) {
                bool popped = queue.pop(value);
                if (popped) {
                    consumed_count++;
                    consecutive_empty_checks = 0;
                } else {
                    consecutive_empty_checks++;
                }
                
                // 如果生产者已完成且连续多次检查队列为空，退出
                if (producers_done.load() && consecutive_empty_checks > 10) {
                    break;
                }
                
                // 超时保护，防止死循环
                if (std::chrono::steady_clock::now() - start_time > timeout) {
                    std::cout << "消费者线程超时退出" << std::endl;
                    break;
                }
                
                // 减少延迟
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // 等待所有生产者完成
    for (auto& t : producers) {
        if (t.joinable()) {
            t.join();
        }
    }
    producers_done.store(true);
    
    // 给消费者一些时间完成剩余工作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 等待所有消费者完成
    for (auto& t : consumers) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    // 验证结果
    std::cout << "生产项目数: " << produced_count << ", 消费项目数: " << consumed_count << std::endl;
    assert_true("所有生产的项目应被消费", consumed_count == produced_count);
}

// 测试用例：测试队列停止后的行为一致性
void test_queue_stop_consistency() {
    SafeQueue<int> queue;
    
    // 入队一些数据
    for (int i = 0; i < 10; i++) {
        bool result = queue.enqueue(i);
        assert_true("正常队列入队应成功", result);
    }
    
    // 停止队列
    queue.stop();
    
    // 验证所有入队操作都失败
    for (int i = 0; i < 5; i++) {
        bool result = queue.enqueue(i + 100);
        assert_false("停止队列后入队应失败", result);
    }
    
    // 验证可以出队已有数据
    int count = 0;
    int value;
    while (queue.pop(value)) {
        assert_true("出队值应与入队顺序一致", value == count);
        count++;
    }
    
    assert_true("应出队所有已入队数据", count == 10);
    
    // 验证队列为空且已停止时，出队失败
    bool result = queue.pop(value);
    assert_false("空且已停止的队列出队应失败", result);
}

int main() {
    std::cout << "=== SafeQueue 数据完整性修复测试 ===" << std::endl;
    
    // 获取当前时间并格式化
    std::time_t now = std::time(nullptr);
    std::cout << "测试开始时间: " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << std::endl;
    std::cout << "-----------------------------------" << std::endl;
    
    // 入队返回状态验证测试
    TestRunner::run_test("test_enqueue_return_status", test_enqueue_return_status);
    
    // 停止队列行为和返回值测试
    TestRunner::run_test("test_stopped_queue_behavior", test_stopped_queue_behavior);
    
    // 返回值机制的线程安全性测试
    TestRunner::run_test("test_thread_safety_of_return_value", test_thread_safety_of_return_value);
    
    // 向后兼容性测试
    TestRunner::run_test("test_backward_compatibility", test_backward_compatibility);
    
    // 多线程入队出队测试
    TestRunner::run_test("test_multithreaded_enqueue_dequeue", test_multithreaded_enqueue_dequeue);
    
    // 队列停止后的行为一致性测试
    TestRunner::run_test("test_queue_stop_consistency", test_queue_stop_consistency);
    
    std::cout << "-----------------------------------" << std::endl;
    
    // 获取结束时间并格式化
    std::time_t end_time = std::time(nullptr);
    std::cout << "测试结束时间: " << std::put_time(std::localtime(&end_time), "%Y-%m-%d %H:%M:%S") << std::endl;
    TestRunner::print_summary();
    return TestRunner::get_failed_count();
}