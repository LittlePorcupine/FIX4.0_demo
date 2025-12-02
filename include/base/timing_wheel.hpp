#pragma once

#include <vector>
#include <list>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <climits>

namespace fix40 {

using TimerTask = std::function<void()>;
using TimerTaskId = uint64_t;

constexpr int MAX_SAFE_DELAY_MS = INT_MAX / 1000;
constexpr TimerTaskId INVALID_TIMER_ID = 0;

/**
 * @class TimingWheel
 * @brief 支持周期性任务的时间轮定时器
 *
 * 特性：
 * - O(1) 添加任务
 * - 支持一次性任务和周期性任务
 * - 支持取消任务
 * - 线程安全
 */
class TimingWheel {
public:
    TimingWheel(int wheel_size, int tick_interval_ms)
        : wheel_size_(wheel_size),
          tick_interval_ms_(tick_interval_ms),
          wheel_(wheel_size),
          next_task_id_(1) {}

    /**
     * @brief 添加一次性任务
     * @param delay_ms 延迟时间（毫秒）
     * @param task 任务回调
     * @return 任务 ID，可用于取消任务
     */
    TimerTaskId add_task(int delay_ms, TimerTask task) {
        return add_task_internal(delay_ms, std::move(task), false);
    }

    /**
     * @brief 添加周期性任务
     * @param interval_ms 执行间隔（毫秒）
     * @param task 任务回调
     * @return 任务 ID，可用于取消任务
     */
    TimerTaskId add_periodic_task(int interval_ms, TimerTask task) {
        return add_task_internal(interval_ms, std::move(task), true);
    }

    /**
     * @brief 取消任务
     * @param id 任务 ID
     */
    void cancel_task(TimerTaskId id) {
        if (id == INVALID_TIMER_ID) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_map_.find(id);
        if (it != task_map_.end()) {
            it->second->cancelled = true;
        }
    }

    /**
     * @brief 时间轮前进一格，执行到期任务
     */
    void tick() {
        std::list<std::shared_ptr<TimerNode>> tasks_to_run;
        std::list<std::shared_ptr<TimerNode>> tasks_to_reschedule;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            current_tick_ = (current_tick_ + 1) % wheel_size_;
            auto& slot = wheel_[current_tick_];

            for (auto it = slot.begin(); it != slot.end(); ) {
                auto& node = *it;

                // 已取消的任务，从映射中移除
                if (node->cancelled) {
                    task_map_.erase(node->id);
                    it = slot.erase(it);
                    continue;
                }

                if (node->remaining_laps > 0) {
                    node->remaining_laps--;
                    ++it;
                } else {
                    // 任务到期
                    tasks_to_run.push_back(node);

                    // 周期性任务需要重新调度
                    if (node->is_periodic) {
                        tasks_to_reschedule.push_back(node);
                    } else {
                        // 一次性任务，从映射中移除
                        task_map_.erase(node->id);
                    }

                    it = slot.erase(it);
                }
            }

            // 重新调度周期性任务
            for (auto& node : tasks_to_reschedule) {
                if (!node->cancelled) {
                    int target_slot = (current_tick_ + node->interval_ticks) % wheel_size_;
                    node->remaining_laps = (node->interval_ticks - 1) / wheel_size_;
                    wheel_[target_slot].push_back(node);
                }
            }
        }

        // 在锁外执行任务，避免死锁
        for (auto& node : tasks_to_run) {
            if (!node->cancelled && node->task) {
                node->task();
            }
        }
    }

private:
    struct TimerNode {
        TimerTaskId id;
        int remaining_laps;
        int interval_ticks;  // 周期间隔（tick 数），0 表示一次性任务
        bool is_periodic;
        bool cancelled;
        TimerTask task;

        TimerNode(TimerTaskId id_, int laps, int interval, bool periodic, TimerTask t)
            : id(id_), remaining_laps(laps), interval_ticks(interval),
              is_periodic(periodic), cancelled(false), task(std::move(t)) {}
    };

    TimerTaskId add_task_internal(int delay_ms, TimerTask task, bool periodic) {
        if (delay_ms <= 0 || !task) {
            return INVALID_TIMER_ID;
        }

        if (delay_ms > MAX_SAFE_DELAY_MS) {
            return INVALID_TIMER_ID;
        }

        int ticks_to_wait = (delay_ms + tick_interval_ms_ - 1) / tick_interval_ms_;

        std::lock_guard<std::mutex> lock(mutex_);

        TimerTaskId id = next_task_id_++;
        int remaining_laps = (ticks_to_wait - 1) / wheel_size_;
        int target_slot = (current_tick_ + ticks_to_wait) % wheel_size_;

        auto node = std::make_shared<TimerNode>(
            id, remaining_laps, ticks_to_wait, periodic, std::move(task)
        );

        wheel_[target_slot].push_back(node);
        task_map_[id] = node;

        return id;
    }

    const int wheel_size_;
    const int tick_interval_ms_;
    int current_tick_ = 0;

    std::vector<std::list<std::shared_ptr<TimerNode>>> wheel_;
    std::unordered_map<TimerTaskId, std::shared_ptr<TimerNode>> task_map_;
    std::atomic<TimerTaskId> next_task_id_;
    std::mutex mutex_;
};

} // namespace fix40
