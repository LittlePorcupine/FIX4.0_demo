/**
 * @file logger.hpp
 * @brief 线程安全的日志输出工具
 *
 * 提供简洁的流式日志接口，确保多线程环境下日志输出的完整性。
 */

#pragma once

#include <mutex>
#include <sstream>
#include <unistd.h>

namespace fix40 {

/**
 * @class Logger
 * @brief 线程安全的日志输出器（单例模式）
 *
 * 确保每条日志完整输出，不会被其他线程的输出打断。
 * 使用 POSIX write() 系统调用保证原子性写入。
 *
 * @par 使用示例
 * @code
 * LOG() << "Connection established, fd=" << fd;
 * LOG() << "Received message: " << msg;
 * @endcode
 *
 * @note 日志会自动在末尾添加换行符
 */
class Logger {
public:
    /**
     * @brief 获取 Logger 单例实例
     * @return Logger& 单例引用
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    /**
     * @class LogStream
     * @brief 日志流对象，支持流式输出
     *
     * 该对象在析构时将缓冲区内容原子性地写入标准输出。
     * 通过 RAII 机制确保日志完整输出。
     */
    class LogStream {
    public:
        /**
         * @brief 构造日志流
         * @param mtx 用于保护输出的互斥锁引用
         */
        LogStream(std::mutex& mtx) : mtx_(mtx) {}
        
        /**
         * @brief 析构时输出日志
         *
         * 自动添加换行符，并使用 write() 系统调用原子性写入。
         */
        ~LogStream() {
            buffer_ << '\n';
            std::string str = buffer_.str();
            std::lock_guard<std::mutex> lock(mtx_);
            // 使用 write() 系统调用，单次调用是原子的
            ::write(STDOUT_FILENO, str.c_str(), str.size());
        }

        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;
        
        /**
         * @brief 移动构造函数
         * @param other 源对象
         */
        LogStream(LogStream&& other) noexcept 
            : mtx_(other.mtx_), buffer_(std::move(other.buffer_)) {}

        /**
         * @brief 流式输出操作符
         * @tparam T 值类型（需支持 ostream 输出）
         * @param value 要输出的值
         * @return LogStream& 返回自身以支持链式调用
         */
        template<typename T>
        LogStream& operator<<(const T& value) {
            buffer_ << value;
            return *this;
        }

    private:
        std::mutex& mtx_;           ///< 互斥锁引用
        std::ostringstream buffer_; ///< 日志缓冲区
    };

    /**
     * @brief 创建日志流对象
     * @return LogStream 日志流对象
     */
    LogStream log() {
        return LogStream(mutex_);
    }

private:
    Logger() = default;
    std::mutex mutex_; ///< 保护日志输出的互斥锁
};

/**
 * @def LOG()
 * @brief 日志输出宏
 *
 * 使用方式：LOG() << "message" << value;
 */
#define LOG() fix40::Logger::instance().log()

} // namespace fix40
