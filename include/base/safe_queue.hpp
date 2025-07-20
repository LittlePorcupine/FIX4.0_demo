#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

namespace fix40 {

template <typename T>
class SafeQueue {
public:
    SafeQueue() : stop_(false) {}

    void enqueue(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            return; // Or throw an exception
        }
        queue_.push(std::move(value));
        cond_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || stop_; });
        if (stop_ && queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty() || stop_) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        cond_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    bool stop_;
};

} // namespace fix40