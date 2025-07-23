#pragma once

#include <vector>
#include <list>
#include <functional>
#include <memory>
#include <mutex>
#include <climits>


namespace fix40 {

// 可以被 TimingWheel 安排执行的任务
using TimerTask = std::function<void()>;

// TimingWheel 安全常量
constexpr int MAX_SAFE_DELAY_MS = INT_MAX / 1000;  // 防止整数溢出的实用上限

/**
 * @class TimingWheel
 * @brief 简单的哈希定时轮，高效管理许多定时器
 *
 * 该实现在添加任务和轮转时是线程安全的
 */
class TimingWheel {
public:
    /**
     * @param wheel_size 轮中的槽数（即挑数），例如 60 代表一分钟
     * @param tick_interval_ms 每次挑的时间间隔（毫秒），例如 1000 代表 1 秒
     */
    TimingWheel(int wheel_size, int tick_interval_ms)
        : wheel_size_(wheel_size),
          tick_interval_ms_(tick_interval_ms),
          wheel_(wheel_size) {}

    /**
     * @brief 设置任务稍后执行
     * @param delay_ms 在执行任务前的延迟时间毫秒
     * @param task 需要执行的任务
     */
    void add_task(int delay_ms, TimerTask task) {
        if (delay_ms <= 0 || !task) {
            return;
        }

        // 安全检查：防止延迟值过大导致整数溢出
        if (delay_ms > MAX_SAFE_DELAY_MS) {
            return;  // 静默忽略过大的延迟值
        }

        // 计算在轮上的位置
        int ticks_to_wait = (delay_ms + tick_interval_ms_ - 1) / tick_interval_ms_; // 向上取整

        std::lock_guard<std::mutex> lock(mutex_);

        int remaining_laps = (ticks_to_wait - 1) / wheel_size_;
        int target_slot = (current_tick_ + ticks_to_wait) % wheel_size_;

        wheel_[target_slot].push_back({remaining_laps, std::move(task)});
    }

    /**
     * @brief 轮直接向前翻一格，处理当前槽中的任务
     * 应该被外部定时器定期调用
     */
    void tick() {
        std::list<TimerNode> tasks_to_run;
        // int current_tick_val; // 不再需要

        {
            std::lock_guard<std::mutex> lock(mutex_);

            current_tick_ = (current_tick_ + 1) % wheel_size_;
            // current_tick_val = current_tick_; // 不再需要

            auto& slot_tasks = wheel_[current_tick_];

            for (auto it = slot_tasks.begin(); it != slot_tasks.end(); /* 不增加 */) {
                if (it->remaining_laps > 0) {
                    it->remaining_laps--;
                    ++it;
                } else {
                    // 任务到期，移至临时列表，在锁外执行
                    tasks_to_run.splice(tasks_to_run.end(), slot_tasks, it++);
                }
            }
        }

        /* 此处为诊断日志
        if (!tasks_to_run.empty()) {
            std::cout << "[TimingWheel] Tick " << current_tick_val << ", executing " << tasks_to_run.size() << " tasks." << std::endl;
        }
        */

        // 在不拥有锁的状态下执行任务，防止死锁
        for (const auto& node : tasks_to_run) {
            node.task();
        }
    }

private:
    struct TimerNode {
        int remaining_laps;
        TimerTask task;
    };

    const int wheel_size_;
    const int tick_interval_ms_;
    int current_tick_ = 0;

    std::vector<std::list<TimerNode>> wheel_;
    std::mutex mutex_;
};
} // fix40 名称空间结束
