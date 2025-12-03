#pragma once

#include <iostream>
#include <mutex>
#include <sstream>

namespace fix40 {

/**
 * 线程安全的日志输出
 * 
 * 确保每条日志完整输出，不会被其他线程的输出打断。
 * 使用方式：LOG() << "message" << value;
 */
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    // 使用 RAII 风格，先缓冲到 stringstream，析构时一次性输出
    class LogStream {
    public:
        LogStream(std::mutex& mtx) : mtx_(mtx) {}
        
        ~LogStream() {
            std::lock_guard<std::mutex> lock(mtx_);
            std::cout << buffer_.str() << std::endl;
        }

        // 禁止拷贝
        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;
        
        // 允许移动
        LogStream(LogStream&& other) noexcept 
            : mtx_(other.mtx_), buffer_(std::move(other.buffer_)) {}

        template<typename T>
        LogStream& operator<<(const T& value) {
            buffer_ << value;
            return *this;
        }

    private:
        std::mutex& mtx_;
        std::ostringstream buffer_;
    };

    LogStream log() {
        return LogStream(mutex_);
    }

private:
    Logger() = default;
    std::mutex mutex_;
};

// 便捷宏
#define LOG() fix40::Logger::instance().log()

} // namespace fix40
