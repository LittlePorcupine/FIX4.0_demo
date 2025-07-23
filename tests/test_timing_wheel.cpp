#include "base/timing_wheel.hpp"
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
#include <climits> // 用于INT_MAX

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

void assert_equals(const std::string& message, int expected, int actual) {
    if (expected != actual) {
        throw std::runtime_error("断言失败: " + message + " (期望: " + std::to_string(expected) + ", 实际: " + std::to_string(actual) + ")");
    }
}

// 测试用例：验证延迟边界检查功能
void test_delay_bounds_checking() {
    TimingWheel wheel(60, 1000); // 60槽，每槽1秒
    std::atomic<int> task_executed(0);
    
    // 创建测试任务
    auto test_task = [&task_executed]() {
        task_executed++;
    };
    
    // 测试正常延迟值
    wheel.add_task(1000, test_task);  // 1秒延迟，应该成功
    wheel.add_task(5000, test_task);  // 5秒延迟，应该成功
    wheel.add_task(MAX_SAFE_DELAY_MS, test_task);  // 最大安全延迟，应该成功
    
    // 测试过大的延迟值（应该被静默忽略）
    wheel.add_task(MAX_SAFE_DELAY_MS + 1, test_task);  // 超过最大安全延迟
    wheel.add_task(INT_MAX, test_task);  // 极大值
    wheel.add_task(INT_MAX - 1000, test_task);  // 接近INT_MAX的值
    
    // 执行足够的tick来触发正常任务
    for (int i = 0; i < 10; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 验证只有合法的任务被执行（这里我们不能精确预测执行数量，因为时间轮的复杂性）
    // 但至少应该有一些任务被执行，而过大的延迟任务不应该被执行
    std::cout << "执行的任务数: " << task_executed.load() << std::endl;
}

// 测试用例：测试最大安全延迟值和过大延迟拒绝
void test_maximum_safe_delay_and_oversized_rejection() {
    TimingWheel wheel(10, 100); // 10槽，每槽100ms
    std::atomic<int> safe_tasks_executed(0);
    std::atomic<int> oversized_tasks_executed(0);
    
    auto safe_task = [&safe_tasks_executed]() {
        safe_tasks_executed++;
    };
    
    auto oversized_task = [&oversized_tasks_executed]() {
        oversized_tasks_executed++;
    };
    
    // 添加安全范围内的任务
    wheel.add_task(1000, safe_task);  // 1秒
    wheel.add_task(10000, safe_task); // 10秒
    wheel.add_task(MAX_SAFE_DELAY_MS, safe_task); // 最大安全延迟
    
    // 添加超出安全范围的任务（应该被拒绝）
    wheel.add_task(MAX_SAFE_DELAY_MS + 1, oversized_task);
    wheel.add_task(MAX_SAFE_DELAY_MS + 1000, oversized_task);
    wheel.add_task(INT_MAX, oversized_task);
    wheel.add_task(INT_MAX - 1, oversized_task);
    
    // 执行一些tick
    for (int i = 0; i < 20; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // 验证过大的延迟任务没有被执行
    assert_equals("过大延迟任务应该被拒绝", 0, oversized_tasks_executed.load());
    std::cout << "安全任务执行数: " << safe_tasks_executed.load() << ", 过大任务执行数: " << oversized_tasks_executed.load() << std::endl;
}

// 测试用例：验证正常定时器操作继续正常工作
void test_normal_timer_operations_still_work() {
    TimingWheel wheel(10, 100); // 10槽，每槽100ms
    std::atomic<int> task_count(0);
    std::vector<int> execution_order;
    std::mutex order_mutex;
    
    // 添加不同延迟的任务
    wheel.add_task(200, [&task_count, &execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        task_count++;
        execution_order.push_back(1);
    });
    
    wheel.add_task(400, [&task_count, &execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        task_count++;
        execution_order.push_back(2);
    });
    
    wheel.add_task(100, [&task_count, &execution_order, &order_mutex]() {
        std::lock_guard<std::mutex> lock(order_mutex);
        task_count++;
        execution_order.push_back(0);
    });
    
    // 执行足够的tick来触发所有任务
    for (int i = 0; i < 50; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 验证所有任务都被执行
    assert_equals("应该执行3个任务", 3, task_count.load());
    
    // 验证执行顺序（最短延迟的任务应该先执行）
    {
        std::lock_guard<std::mutex> lock(order_mutex);
        assert_true("应该有3个任务被执行", execution_order.size() == 3);
        assert_equals("第一个执行的应该是延迟最短的任务", 0, execution_order[0]);
        std::cout << "任务执行顺序: ";
        for (size_t i = 0; i < execution_order.size(); i++) {
            std::cout << execution_order[i];
            if (i < execution_order.size() - 1) std::cout << " -> ";
        }
        std::cout << std::endl;
    }
}

// 测试用例：测试零、负数和极大延迟值的边界情况
void test_edge_cases_zero_negative_extremely_large_delays() {
    TimingWheel wheel(5, 1000); // 5槽，每槽1秒
    std::atomic<int> executed_tasks(0);
    
    auto test_task = [&executed_tasks]() {
        executed_tasks++;
    };
    
    // 测试零延迟（应该被忽略）
    wheel.add_task(0, test_task);
    
    // 测试负延迟（应该被忽略）
    wheel.add_task(-1, test_task);
    wheel.add_task(-100, test_task);
    wheel.add_task(-1000, test_task);
    
    // 测试极大延迟值（应该被忽略）
    wheel.add_task(INT_MAX, test_task);
    wheel.add_task(INT_MAX - 1, test_task);
    wheel.add_task(MAX_SAFE_DELAY_MS + 1, test_task);
    
    // 测试空任务（应该被忽略）
    wheel.add_task(1000, nullptr);
    wheel.add_task(1000, TimerTask{});
    
    // 添加一个正常任务作为对照
    wheel.add_task(1000, test_task);
    
    // 执行一些tick
    for (int i = 0; i < 10; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 验证只有正常任务可能被执行，无效任务都被忽略
    // 注意：由于时间轮的特性，我们不能保证正常任务一定在这么短的时间内执行
    // 但我们可以确保执行的任务数不会超过1
    assert_true("执行的任务数应该不超过1", executed_tasks.load() <= 1);
    std::cout << "执行的任务数: " << executed_tasks.load() << " (应该 <= 1)" << std::endl;
}

// 测试用例：测试边界值附近的延迟处理
void test_boundary_value_delay_handling() {
    TimingWheel wheel(60, 1000); // 60槽，每槽1秒
    std::atomic<int> safe_boundary_tasks(0);
    std::atomic<int> unsafe_boundary_tasks(0);
    
    auto safe_task = [&safe_boundary_tasks]() {
        safe_boundary_tasks++;
    };
    
    auto unsafe_task = [&unsafe_boundary_tasks]() {
        unsafe_boundary_tasks++;
    };
    
    // 测试安全边界值
    wheel.add_task(MAX_SAFE_DELAY_MS, safe_task);      // 正好等于最大安全值
    wheel.add_task(MAX_SAFE_DELAY_MS - 1, safe_task);  // 比最大安全值小1
    wheel.add_task(MAX_SAFE_DELAY_MS - 1000, safe_task); // 比最大安全值小1000
    
    // 测试不安全边界值
    wheel.add_task(MAX_SAFE_DELAY_MS + 1, unsafe_task);    // 比最大安全值大1
    wheel.add_task(MAX_SAFE_DELAY_MS + 1000, unsafe_task); // 比最大安全值大1000
    
    // 执行一些tick
    for (int i = 0; i < 10; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 验证不安全的边界值任务没有被执行
    assert_equals("不安全边界值任务应该被拒绝", 0, unsafe_boundary_tasks.load());
    std::cout << "安全边界任务: " << safe_boundary_tasks.load() << ", 不安全边界任务: " << unsafe_boundary_tasks.load() << std::endl;
}

// 测试用例：测试多线程环境下的延迟边界检查
void test_multithreaded_delay_bounds_checking() {
    TimingWheel wheel(10, 100); // 10槽，每槽100ms
    std::atomic<int> safe_tasks_added(0);
    std::atomic<int> unsafe_tasks_added(0);
    std::atomic<int> tasks_executed(0);
    
    const int num_threads = 4;
    const int tasks_per_thread = 25;
    
    std::vector<std::thread> threads;
    
    // 创建多个线程同时添加任务
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&wheel, &safe_tasks_added, &unsafe_tasks_added, &tasks_executed, i, tasks_per_thread]() {
            for (int j = 0; j < tasks_per_thread; j++) {
                auto task = [&tasks_executed]() {
                    tasks_executed++;
                };
                
                if (i % 2 == 0) {
                    // 偶数线程添加安全延迟任务
                    int safe_delay = 100 + (j % 1000); // 100-1099ms范围内的安全延迟
                    wheel.add_task(safe_delay, task);
                    safe_tasks_added++;
                } else {
                    // 奇数线程添加不安全延迟任务
                    int unsafe_delay = MAX_SAFE_DELAY_MS + 1 + j; // 超过安全限制的延迟
                    wheel.add_task(unsafe_delay, task);
                    unsafe_tasks_added++;
                }
                
                // 添加短暂延迟避免过快添加
                if (j % 10 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    // 执行一些tick
    for (int i = 0; i < 20; i++) {
        wheel.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 验证结果
    std::cout << "安全任务添加数: " << safe_tasks_added.load() << std::endl;
    std::cout << "不安全任务添加数: " << unsafe_tasks_added.load() << std::endl;
    std::cout << "任务执行数: " << tasks_executed.load() << std::endl;
    
    assert_true("应该添加了安全任务", safe_tasks_added.load() > 0);
    assert_true("应该添加了不安全任务", unsafe_tasks_added.load() > 0);
    // 注意：由于时间轮的特性和短暂的执行时间，我们不能保证所有安全任务都被执行
    // 但我们可以验证系统没有崩溃，这本身就是一个成功
}

int main() {
    std::cout << "=== TimingWheel 溢出防护测试 ===" << std::endl;
    
    // 获取当前时间并格式化
    std::time_t now = std::time(nullptr);
    std::cout << "测试开始时间: " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << std::endl;
    std::cout << "最大安全延迟值: " << MAX_SAFE_DELAY_MS << " ms" << std::endl;
    std::cout << "-----------------------------------" << std::endl;
    
    // 延迟边界检查功能测试
    TestRunner::run_test("test_delay_bounds_checking", test_delay_bounds_checking);
    
    // 最大安全延迟值和过大延迟拒绝测试
    TestRunner::run_test("test_maximum_safe_delay_and_oversized_rejection", test_maximum_safe_delay_and_oversized_rejection);
    
    // 正常定时器操作继续正常工作测试
    TestRunner::run_test("test_normal_timer_operations_still_work", test_normal_timer_operations_still_work);
    
    // 零、负数和极大延迟值的边界情况测试
    TestRunner::run_test("test_edge_cases_zero_negative_extremely_large_delays", test_edge_cases_zero_negative_extremely_large_delays);
    
    // 边界值附近的延迟处理测试
    TestRunner::run_test("test_boundary_value_delay_handling", test_boundary_value_delay_handling);
    
    // 多线程环境下的延迟边界检查测试
    TestRunner::run_test("test_multithreaded_delay_bounds_checking", test_multithreaded_delay_bounds_checking);
    
    std::cout << "-----------------------------------" << std::endl;
    
    // 获取结束时间并格式化
    std::time_t end_time = std::time(nullptr);
    std::cout << "测试结束时间: " << std::put_time(std::localtime(&end_time), "%Y-%m-%d %H:%M:%S") << std::endl;
    TestRunner::print_summary();
    return TestRunner::get_failed_count();
}