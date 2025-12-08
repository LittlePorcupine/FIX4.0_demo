/**
 * @file timing_wheel.hpp
 * @brief 时间轮定时器实现
 *
 * 提供高效的定时任务管理，支持一次性和周期性任务，
 * 适用于心跳检测、超时管理等场景。
 */

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

/// 定时任务回调函数类型
using TimerTask = std::function<void()>;
/// 定时任务唯一标识符类型
using TimerTaskId = uint64_t;

/// 最大安全延迟时间（毫秒），防止整数溢出
constexpr int MAX_SAFE_DELAY_MS = INT_MAX / 1000;
/// 无效的定时任务 ID
constexpr TimerTaskId INVALID_TIMER_ID = 0;

/**
 * @class TimingWheel
 * @brief 支持周期性任务的时间轮定时器
 *
 * 时间轮是一种高效的定时器实现，将时间划分为固定数量的槽位，
 * 每个槽位存储在该时刻到期的任务列表。
 *
 * @par 特性
 * - O(1) 时间复杂度添加任务
 * - 支持一次性任务和周期性任务
 * - 支持取消任务
 * - 线程安全
 *
 * @par 工作原理
 * 1. 时间轮由 N 个槽位组成，每个槽位代表一个时间间隔
 * 2. 指针每次 tick() 前进一格，执行当前槽位的到期任务
 * 3. 对于延迟超过一圈的任务，使用 remaining_laps 记录剩余圈数
 *
 * @par 使用示例
 * @code
 * TimingWheel wheel(60, 1000);  // 60 个槽，每槽 1 秒
 * 
 * // 添加一次性任务，5 秒后执行
 * auto id = wheel.add_task(5000, []() { std::cout << "Timeout!" << std::endl; });
 * 
 * // 添加周期性任务，每 30 秒执行一次
 * wheel.add_periodic_task(30000, []() { send_heartbeat(); });
 * 
 * // 取消任务
 * wheel.cancel_task(id);
 * 
 * // 在定时器回调中驱动时间轮
 * reactor.add_timer(1000, [&wheel](int) { wheel.tick(); });
 * @endcode
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
     * @param delay_ms 延迟时间（毫秒），必须大于 0 且不超过 MAX_SAFE_DELAY_MS
     * @param task 任务回调函数
     * @return TimerTaskId 任务 ID，可用于取消任务；失败返回 INVALID_TIMER_ID
     *
     * @note 任务将在约 delay_ms 毫秒后执行一次，然后自动移除
     */
    TimerTaskId add_task(int delay_ms, TimerTask task) {
        return add_task_internal(delay_ms, std::move(task), false);
    }

    /**
     * @brief 添加周期性任务
     * @param interval_ms 执行间隔（毫秒），必须大于 0 且不超过 MAX_SAFE_DELAY_MS
     * @param task 任务回调函数
     * @return TimerTaskId 任务 ID，可用于取消任务；失败返回 INVALID_TIMER_ID
     *
     * @note 任务将每隔 interval_ms 毫秒执行一次，直到被取消
     */
    TimerTaskId add_periodic_task(int interval_ms, TimerTask task) {
        return add_task_internal(interval_ms, std::move(task), true);
    }

    /**
     * @brief 取消任务
     * @param id 要取消的任务 ID
     *
     * 标记任务为已取消状态，任务将在下次 tick() 时被清理。
     * 如果 id 为 INVALID_TIMER_ID 或任务不存在，则无操作。
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
     *
     * 该方法应由外部定时器周期性调用，调用间隔应等于 tick_interval_ms。
     *
     * @par 执行流程
     * 1. 指针前进一格
     * 2. 遍历当前槽位的任务列表
     * 3. 跳过已取消的任务
     * 4. 对于 remaining_laps > 0 的任务，减少圈数
     * 5. 执行到期任务
     * 6. 周期性任务重新调度到下一周期
     *
     * @note 任务回调在锁外执行，避免死锁
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
    /**
     * @struct TimerNode
     * @brief 定时任务节点
     */
    struct TimerNode {
        TimerTaskId id;       ///< 任务唯一标识符
        int remaining_laps;   ///< 剩余圈数（用于延迟超过一圈的任务）
        int interval_ticks;   ///< 周期间隔（tick 数），用于周期性任务重新调度
        bool is_periodic;     ///< 是否为周期性任务
        bool cancelled;       ///< 是否已取消
        TimerTask task;       ///< 任务回调函数

        /**
         * @brief 构造定时任务节点
         */
        TimerNode(TimerTaskId id_, int laps, int interval, bool periodic, TimerTask t)
            : id(id_), remaining_laps(laps), interval_ticks(interval),
              is_periodic(periodic), cancelled(false), task(std::move(t)) {}
    };

    /**
     * @brief 内部添加任务实现
     * @param delay_ms 延迟/间隔时间（毫秒）
     * @param task 任务回调
     * @param periodic 是否为周期性任务
     * @return TimerTaskId 任务 ID
     */
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

    const int wheel_size_;        ///< 时间轮槽位数量
    const int tick_interval_ms_;  ///< 每个槽位代表的时间间隔（毫秒）
    int current_tick_ = 0;        ///< 当前指针位置

    /// 时间轮槽位数组，每个槽位是一个任务链表
    std::vector<std::list<std::shared_ptr<TimerNode>>> wheel_;
    /// 任务 ID 到任务节点的映射，用于快速查找和取消
    std::unordered_map<TimerTaskId, std::shared_ptr<TimerNode>> task_map_;
    /// 下一个可用的任务 ID（原子递增）
    std::atomic<TimerTaskId> next_task_id_;
    /// 保护时间轮数据结构的互斥锁
    std::mutex mutex_;
};

} // namespace fix40
