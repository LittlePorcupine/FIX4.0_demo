#include "core/connection.hpp"
#include "fix/session.hpp"
#include "core/reactor.hpp"
#include "base/thread_pool.hpp"
#include "base/config.hpp"

#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <vector>

namespace fix40 {

Connection::Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session,
                       ThreadPool* thread_pool, size_t thread_index)
    : fd_(fd),
      reactor_(reactor),
      session_(std::move(session)),
      thread_pool_(thread_pool),
      thread_index_(thread_index),
      frame_decoder_(
          Config::instance().get_int("protocol", "max_buffer_size", 1048576),
          Config::instance().get_int("protocol", "max_body_length", 4096)
      ) {
    std::cout << "Connection created for fd " << fd_ 
              << ", bindded to thread " << thread_index_ << std::endl;
}

Connection::~Connection() {
    std::cout << "Connection destroyed for fd " << fd_ << std::endl;
    close_fd();
}

void Connection::handle_read() {
    if (is_closed_) return;

    std::vector<char> read_buf(Config::instance().get_int("protocol", "max_buffer_size", 4096));
    ssize_t bytes_read = 0;

    // ET 模式需要一直读到 EAGAIN
    while (true) {
        bytes_read = ::read(fd_, read_buf.data(), read_buf.size());
        if (bytes_read > 0) {
            frame_decoder_.append(read_buf.data(), bytes_read);
        } else {
            break;
        }
    }

    if (bytes_read == 0) {
        session_->on_shutdown("Connection closed by peer.");
        return;
    }

    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            session_->on_io_error("Socket read error.");
            return;
        }
    }

    try {
        std::string raw_msg;
        while (frame_decoder_.next_message(raw_msg)) {
            FixMessage fix_msg = session_->codec_.decode(raw_msg);
            std::cout << "<<< RECV (" << fd_ << "): " << raw_msg << std::endl;
            session_->on_message_received(fix_msg);
        }
    } catch (const std::exception& e) {
        session_->on_io_error("Frame decoder or parser error: " + std::string(e.what()));
    }
}

void Connection::handle_write() {
    if (write_buffer_.empty()) {
        reactor_->modify_fd(fd_, static_cast<uint32_t>(EventType::READ), nullptr);
        return;
    }

    ssize_t sent = ::send(fd_, write_buffer_.c_str(), write_buffer_.length(), 0);

    if (sent >= 0) {
        if (static_cast<size_t>(sent) < write_buffer_.length()) {
            write_buffer_.erase(0, sent);
        } else {
            write_buffer_.clear();
            reactor_->modify_fd(fd_, static_cast<uint32_t>(EventType::READ), nullptr);
        }
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            session_->on_io_error("Socket write error.");
        }
    }
}

void Connection::send(std::string_view data) {
    if (is_closed_) return;

    // 将发送操作派发到绑定的线程执行
    std::string data_copy(data);
    dispatch([this, data_copy = std::move(data_copy)]() {
        do_send(data_copy);
    });
}

void Connection::do_send(const std::string& data) {
    if (is_closed_) return;

    // 现在在绑定的线程中，不需要锁
    if (write_buffer_.empty()) {
        ssize_t sent = ::send(fd_, data.data(), data.length(), 0);
        if (sent >= 0) {
            if (static_cast<size_t>(sent) < data.length()) {
                write_buffer_.append(data.substr(sent));
            } else {
                return;  // 全部发送完成
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                write_buffer_.append(data);
            } else {
                session_->on_io_error("Initial send error");
                return;
            }
        }
    } else {
        write_buffer_.append(data);
    }

    // 有待发送数据，注册写事件
    if (!write_buffer_.empty()) {
        // 注意：这里需要捕获 weak_ptr 避免循环引用
        std::weak_ptr<Connection> weak_self = shared_from_this();
        reactor_->modify_fd(fd_, 
            static_cast<uint32_t>(EventType::READ) | static_cast<uint32_t>(EventType::WRITE),
            [weak_self](int) {
                if (auto self = weak_self.lock()) {
                    // 写事件也派发到绑定线程
                    self->dispatch([self]() {
                        self->handle_write();
                    });
                }
            });
    }
}

void Connection::dispatch(std::function<void()> task) {
    if (thread_pool_) {
        thread_pool_->enqueue_to(thread_index_, std::move(task));
    }
}

void Connection::shutdown() {
    std::cout << "Shutting down connection for fd " << fd_ << std::endl;
    if (reactor_ && !is_closed_) {
        reactor_->remove_fd(fd_);
    }
    close_fd();
}

void Connection::close_fd() {
    if (!is_closed_.exchange(true)) {
        ::shutdown(fd_, SHUT_RDWR);
        close(fd_);
    }
}

} // namespace fix40
