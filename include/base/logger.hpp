#pragma once

#include <mutex>
#include <sstream>
#include <unistd.h>

namespace fix40 {

/**
 * 线程安全的日志输出
 * 
 * 确保每条日志完整输出，不会被其他线程的输出打断。
 * 使用 write() 系统调用保证原子性。
 * 使用方式：LOG() << "message" << value;
 */
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    class LogStream {
    public:
        LogStream(std::mutex& mtx) : mtx_(mtx) {}
        
        ~LogStream() {
            buffer_ << '\n';
            std::string str = buffer_.str();
            std::lock_guard<std::mutex> lock(mtx_);
            // 使用 write() 系统调用，单次调用是原子的
            ::write(STDOUT_FILENO, str.c_str(), str.size());
        }

        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;
        
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

#define LOG() fix40::Logger::instance().log()

} // namespace fix40
