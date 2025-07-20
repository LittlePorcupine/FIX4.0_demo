#include "core/connection.hpp"
#include "fix/session.hpp"
#include "core/reactor.hpp"

#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <string.h>
#include <sys/socket.h> // 为了使用 send()
#include <mutex> // 为了线程安全

namespace fix40 {

Connection::Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session)
    : fd_(fd), reactor_(reactor), session_(std::move(session)) {
    std::cout << "Connection created for fd " << fd_ << std::endl;
}

Connection::~Connection() {
    std::cout << "Connection destroyed for fd " << fd_ << std::endl;
    // fd 关闭时对应的端已由系统关闭
    // 但调用 shutdown 能提供更柔和的断连
    // 将其从 reactor 中移除由其他位置处理
    close_fd();
}

void Connection::handle_read() {
    // 防止从已关闭的连接读取
    if (is_closed_) return;

    // ET 模式需要一直读到缓冲空
    while (true) {
        char read_buf[4096];
        ssize_t bytes_read = ::read(fd_, read_buf, sizeof(read_buf));

        if (bytes_read > 0) {
            read_buffer_.append(read_buf, bytes_read);
            if (read_buffer_.size() > kMaxReadBufferSize) {
                session_->on_io_error("Read buffer overflow");
                return;
            }
        } else if (bytes_read == 0) {
            // 连接被对方关闭
            session_->on_io_error("Connection closed by peer.");
            return;
        } else { // bytes_read < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 暂时没有更多数据可读
                break;
            } else {
                // 发生了实际错误
                session_->on_io_error("Socket read error.");
                return;
            }
        }
    }

    // 现在处理累积的缓冲
    while (!read_buffer_.empty()) {
        std::string& buffer = read_buffer_;
        
        // --- FIX 消息分架逻辑 ---
        const auto begin_string_pos = buffer.find("8=FIX.4.0\x01");
        if (begin_string_pos == std::string::npos) {
            // 未找到有效的开头。为了防止缓冲增长，直接清空
            // 更健壮的实现也许会有不同策略
            buffer.clear();
            break; 
        }
        if (begin_string_pos > 0) {
            // 丢弃消息开头前的无用数据
            buffer.erase(0, begin_string_pos);
        }

        const auto body_length_tag_pos = buffer.find("\x01""9=");
        if (body_length_tag_pos == std::string::npos) break; // 未得到 BodyLength 标签所需的数据

        const auto body_length_val_pos = body_length_tag_pos + 3;
        const auto body_length_end_pos = buffer.find('\x01', body_length_val_pos);
        if (body_length_end_pos == std::string::npos) break; // BodyLength 值数据不足

        int body_length = 0;
        try {
            const std::string body_length_str = buffer.substr(body_length_val_pos, body_length_end_pos - body_length_val_pos);
            body_length = std::stoi(body_length_str);
            if (body_length < 0 || body_length > 4096) { // 基本有效性检查
                throw std::runtime_error("Invalid BodyLength value");
            }
        } catch (const std::exception&) {
            session_->on_io_error("Corrupted or invalid BodyLength");
            buffer.clear(); // 清理缓冲以避免循环
            break;
        }
        
        // 计算总的预期消息长度
        const size_t soh_after_body_length_pos = body_length_end_pos + 1;
        const size_t total_msg_len = soh_after_body_length_pos + body_length + 7; // "10=NNN\x01" 为 7 个字符

        if (buffer.size() < total_msg_len) {
            // 数据不足以形成完整消息
            break;
        }

        // 已经抽出一个完整消息，进行处理
        const std::string raw_msg = buffer.substr(0, total_msg_len);
        try {
            std::cout << "<<< RECV (" << fd_ << "): " << raw_msg << std::endl;
            FixMessage msg = session_->codec_.decode(raw_msg);
            session_->on_message_received(std::move(msg));
        } catch (const std::exception& e) {
            session_->on_io_error(std::string("Decode error: ") + e.what());
        }
        
        // 从缓冲中移除已处理的消息
        buffer.erase(0, total_msg_len);
    }
}

void Connection::handle_write() {
    std::lock_guard<std::mutex> lock(write_mutex_);

    if (write_buffer_.empty()) {
        // 可能是伪写事件，或缓冲区已被其他线程清空
        // 此时可以安全地取消写事件注册
        reactor_->modify_fd(fd_, EventType::READ, nullptr);
        return;
    }

    ssize_t sent = ::send(fd_, write_buffer_.c_str(), write_buffer_.length(), 0);

    if (sent >= 0) {
        if (static_cast<size_t>(sent) < write_buffer_.length()) {
            // 部分发送，移除已发送的数据
            write_buffer_.erase(0, sent);
        } else {
            // 全部数据已发送，清空缓冲并取消写事件注册
            write_buffer_.clear();
            reactor_->modify_fd(fd_, EventType::READ, nullptr);
        }
    } else { // sent < 0
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            session_->on_io_error("Socket write error.");
        }
        // 若为 EAGAIN，则等待下一次 handle_write 调用
    }
}

void Connection::send(std::string_view data) {
    if (is_closed_) return;

    std::lock_guard<std::mutex> lock(write_mutex_);

    // 如果缓冲为空，尝试直接发送
    if (write_buffer_.empty()) {
        ssize_t sent = ::send(fd_, data.data(), data.length(), 0);
        if (sent >= 0) {
            if (static_cast<size_t>(sent) < data.length()) {
                // 部分发送，缓冲剩余数据
                write_buffer_.append(data.substr(sent));
            } else {
                // 已全部发送，无需额外处理
                return;
            }
        } else { // sent < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 无法发送任何数据，将全部内容缓冲
                write_buffer_.append(data);
            } else {
                session_->on_io_error("Initial send error");
                return;
            }
        }
    } else {
        // 缓冲区非空，仅追加新数据
        write_buffer_.append(data);
    }

    // 若缓冲区有数据，需要注册写事件
    if (!write_buffer_.empty()) {
        reactor_->modify_fd(fd_, EventType::READ | EventType::WRITE, [this](int){ this->handle_write(); });
    }
}

void Connection::shutdown() {
    std::cout << "Shutting down connection for fd " << fd_ << std::endl;
    // Session 通过此方法启动关闭
    // 这里负责从 IO 监听中移除 fd 并关闭
    if (reactor_ && !is_closed_) {
        reactor_->remove_fd(fd_);
    }
    // 最终清理在 close_fd() 中进行
    close_fd();
}

void Connection::close_fd() {
    if (!is_closed_.exchange(true)) {
        // 首先表明不再发送或接收数据
        // 这是最稳定的关闭方式
        ::shutdown(fd_, SHUT_RDWR);

        // 然后关闭文件描述符
        close(fd_);
    }
}
} // fix40 名称空间结束
