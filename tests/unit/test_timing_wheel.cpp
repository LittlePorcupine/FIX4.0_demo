#include "../catch2/catch.hpp"
#include "base/timing_wheel.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <set>

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


TEST_CASE("TimingWheel task wraps around wheel", "[timing_wheel]") {
    TimingWheel wheel(5, 100);  // 5 slots
    std::atomic<int> counter{0};
    
    // 添加一个需要绕圈的任务（7 ticks，wheel size 是 5）
    wheel.add_task(700, [&counter]() {
        counter++;
    });
    
    // 执行 6 次 tick，任务还不应该执行
    for (int i = 0; i < 6; ++i) {
        wheel.tick();
    }
    REQUIRE(counter == 0);
    
    // 第 7 次 tick，任务应该执行
    wheel.tick();
    REQUIRE(counter == 1);
}

TEST_CASE("TimingWheel periodic task survives multiple laps", "[timing_wheel]") {
    TimingWheel wheel(5, 100);
    std::atomic<int> counter{0};
    
    // 周期性任务，每 2 ticks 执行一次
    wheel.add_periodic_task(200, [&counter]() {
        counter++;
    });
    
    // 执行 10 次 tick，应该执行 5 次
    for (int i = 0; i < 10; ++i) {
        wheel.tick();
    }
    
    REQUIRE(counter == 5);
}

TEST_CASE("TimingWheel cancel during execution", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    TimerTaskId id = INVALID_TIMER_ID;
    
    // 任务执行时取消自己
    id = wheel.add_periodic_task(100, [&counter, &wheel, &id]() {
        counter++;
        if (counter >= 3) {
            wheel.cancel_task(id);
        }
    });
    
    // 执行多次 tick
    for (int i = 0; i < 10; ++i) {
        wheel.tick();
    }
    
    // 应该只执行 3 次
    REQUIRE(counter == 3);
}

TEST_CASE("TimingWheel large delay", "[timing_wheel]") {
    TimingWheel wheel(10, 100);  // 10 slots, 100ms per tick
    std::atomic<int> counter{0};
    
    // 添加一个 5 秒后执行的任务（50 ticks）
    wheel.add_task(5000, [&counter]() {
        counter++;
    });
    
    // 执行 49 次 tick
    for (int i = 0; i < 49; ++i) {
        wheel.tick();
    }
    REQUIRE(counter == 0);
    
    // 第 50 次 tick
    wheel.tick();
    REQUIRE(counter == 1);
}

TEST_CASE("TimingWheel task ID uniqueness", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    
    std::set<TimerTaskId> ids;
    for (int i = 0; i < 100; ++i) {
        TimerTaskId id = wheel.add_task(100, [](){});
        REQUIRE(id != INVALID_TIMER_ID);
        REQUIRE(ids.find(id) == ids.end());  // ID 应该唯一
        ids.insert(id);
    }
}

TEST_CASE("TimingWheel cancel before execution", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    
    TimerTaskId id = wheel.add_task(300, [&counter]() {
        counter++;
    });
    
    // 执行 2 次 tick（任务还没到期）
    wheel.tick();
    wheel.tick();
    REQUIRE(counter == 0);
    
    // 取消任务
    wheel.cancel_task(id);
    
    // 继续执行，任务不应该执行
    wheel.tick();
    wheel.tick();
    wheel.tick();
    
    REQUIRE(counter == 0);
}

TEST_CASE("TimingWheel extremely short delay", "[timing_wheel]") {
    TimingWheel wheel(10, 100);
    std::atomic<int> counter{0};
    
    // 1ms 延迟，应该在第一次 tick 时执行
    wheel.add_task(1, [&counter]() {
        counter++;
    });
    
    wheel.tick();
    REQUIRE(counter == 1);
}
