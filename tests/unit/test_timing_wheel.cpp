#include "../catch2/catch.hpp"
#include "base/timing_wheel.hpp"
#include <atomic>
#include <thread>
#include <chrono>

using namespace fix40;

TEST_CASE("TimingWheel one-shot task execution", "[timing_wheel]") {
    TimingWheel wheel(10, 100);  // 10 slots, 100ms per tick
    std::atomic<int> counter{0};
    
    wheel.add_task(100, [&counter]() {
        counter++;
    });
    
    // 任务还没执行
    REQUIRE(counter == 0);
    
    // 执行一次 tick
    wheel.tick();
    
    // 任务应该执行了
    REQUIRE(counter == 1);
    
    // 再次 tick，一次性任务不应该再执行
    wheel.tick();
    REQUIRE(counter == 1);
}

TEST_CASE("TimingWheel periodic task execution", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    
    wheel.add_periodic_task(100, [&counter]() {
        counter++;
    });
    
    // 执行多次 tick
    wheel.tick();
    REQUIRE(counter == 1);
    
    wheel.tick();
    REQUIRE(counter == 2);
    
    wheel.tick();
    REQUIRE(counter == 3);
}

TEST_CASE("TimingWheel cancel task", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    
    TimerTaskId id = wheel.add_periodic_task(100, [&counter]() {
        counter++;
    });
    
    wheel.tick();
    REQUIRE(counter == 1);
    
    // 取消任务
    wheel.cancel_task(id);
    
    // 再次 tick，任务不应该执行
    wheel.tick();
    REQUIRE(counter == 1);
    
    wheel.tick();
    REQUIRE(counter == 1);
}

TEST_CASE("TimingWheel delayed task", "[timing_wheel]") {
    TimingWheel wheel(10, 100);  // 10 slots, 100ms per tick
    std::atomic<int> counter{0};
    
    // 添加一个 300ms 后执行的任务（需要 3 次 tick）
    wheel.add_task(300, [&counter]() {
        counter++;
    });
    
    wheel.tick();
    REQUIRE(counter == 0);
    
    wheel.tick();
    REQUIRE(counter == 0);
    
    wheel.tick();
    REQUIRE(counter == 1);
}

TEST_CASE("TimingWheel invalid task rejected", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    
    // 零延迟应该被拒绝
    TimerTaskId id1 = wheel.add_task(0, [](){});
    REQUIRE(id1 == INVALID_TIMER_ID);
    
    // 负延迟应该被拒绝
    TimerTaskId id2 = wheel.add_task(-100, [](){});
    REQUIRE(id2 == INVALID_TIMER_ID);
    
    // 空任务应该被拒绝
    TimerTaskId id3 = wheel.add_task(100, nullptr);
    REQUIRE(id3 == INVALID_TIMER_ID);
}

TEST_CASE("TimingWheel cancel invalid task", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    
    // 取消不存在的任务不应该崩溃
    REQUIRE_NOTHROW(wheel.cancel_task(INVALID_TIMER_ID));
    REQUIRE_NOTHROW(wheel.cancel_task(99999));
}

TEST_CASE("TimingWheel multiple tasks same slot", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    
    // 添加多个相同延迟的任务
    wheel.add_task(100, [&counter]() { counter++; });
    wheel.add_task(100, [&counter]() { counter++; });
    wheel.add_task(100, [&counter]() { counter++; });
    
    wheel.tick();
    
    // 所有任务都应该执行
    REQUIRE(counter == 3);
}
