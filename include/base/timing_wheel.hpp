#pragma once

#include <vector>
#include <list>
#include <functional>
#include <memory>
#include <mutex>


namespace fix40 {

// A task that can be scheduled on the TimingWheel.
using TimerTask = std::function<void()>;

/**
 * @class TimingWheel
 * @brief A simple hashed timing wheel for managing a large number of timers efficiently.
 *
 * This implementation is thread-safe for adding tasks and ticking.
 */
class TimingWheel {
public:
    /**
     * @param wheel_size The number of slots (ticks) in the wheel. e.g., 60 for a 1-minute wheel.
     * @param tick_interval_ms The duration of a single tick in milliseconds. e.g., 1000 for 1-second resolution.
     */
    TimingWheel(int wheel_size, int tick_interval_ms)
        : wheel_size_(wheel_size),
          tick_interval_ms_(tick_interval_ms),
          wheel_(wheel_size) {}

    /**
     * @brief Schedules a task to be executed after a certain delay.
     * @param delay_ms The delay in milliseconds before the task is executed.
     * @param task The task to execute.
     */
    void add_task(int delay_ms, TimerTask task) {
        if (delay_ms <= 0 || !task) {
            return;
        }

        // Calculate the position on the wheel
        int ticks_to_wait = (delay_ms + tick_interval_ms_ - 1) / tick_interval_ms_; // Ceiling division

        std::lock_guard<std::mutex> lock(mutex_);
        
        int remaining_laps = (ticks_to_wait - 1) / wheel_size_;
        int target_slot = (current_tick_ + ticks_to_wait) % wheel_size_;

        wheel_[target_slot].push_back({remaining_laps, std::move(task)});
    }

    /**
     * @brief Advances the wheel by one tick, processing any tasks in the current slot.
     * This should be called periodically by an external timer.
     */
    void tick() {
        std::list<TimerNode> tasks_to_run;
        // int current_tick_val; // No longer needed

        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            current_tick_ = (current_tick_ + 1) % wheel_size_;
            // current_tick_val = current_tick_; // No longer needed

            auto& slot_tasks = wheel_[current_tick_];
            
            for (auto it = slot_tasks.begin(); it != slot_tasks.end(); /* no increment */) {
                if (it->remaining_laps > 0) {
                    it->remaining_laps--;
                    ++it;
                } else {
                    // Task is due, move it to a temporary list to be executed outside the lock.
                    tasks_to_run.splice(tasks_to_run.end(), slot_tasks, it++);
                }
            }
        }
        
        /* This was the diagnostic log
        if (!tasks_to_run.empty()) {
            std::cout << "[TimingWheel] Tick " << current_tick_val << ", executing " << tasks_to_run.size() << " tasks." << std::endl;
        }
        */

        // Execute tasks without holding the lock to avoid deadlocks if a task tries to add another task.
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

} // namespace fix40 