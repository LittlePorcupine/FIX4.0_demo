#include "core/connection.hpp"
#include "fix/session.hpp"
#include "core/reactor.hpp"

#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <string.h>
#include <sys/socket.h> // <-- Add this for send()

namespace fix40 {

Connection::Connection(int fd, Reactor* reactor, std::shared_ptr<Session> session)
    : fd_(fd), reactor_(reactor), session_(std::move(session)) {
    std::cout << "Connection created for fd " << fd_ << std::endl;
}

Connection::~Connection() {
    std::cout << "Connection destroyed for fd " << fd_ << std::endl;
    // The socket is closed by the OS when the fd is closed, 
    // but calling shutdown can provide a more graceful disconnection.
    // We remove it from the reactor elsewhere.
}

void Connection::handle_read() {
    char read_buf[4096];
    ssize_t bytes_read = ::read(fd_, read_buf, sizeof(read_buf));

    if (bytes_read > 0) {
        read_buffer_.append(read_buf, bytes_read);
        
        while (!read_buffer_.empty()) {
            std::string& buffer = read_buffer_;
            // A very basic framing logic. A robust implementation would be more complex.
            const auto begin_string_pos = buffer.find("8=FIX.4.0\x01");
            if (begin_string_pos == std::string::npos) {
                // If we can't find a start, and the buffer is getting large, discard it.
                if (buffer.size() > 8192) buffer.clear();
                break;
            }
            if (begin_string_pos > 0) {
                buffer.erase(0, begin_string_pos);
            }
            const auto body_length_tag_pos = buffer.find("\x01""9=");
            if (body_length_tag_pos == std::string::npos) break;
            const auto body_length_val_pos = body_length_tag_pos + 3;
            const auto body_length_end_pos = buffer.find('\x01', body_length_val_pos);
            if (body_length_end_pos == std::string::npos) break;
            const std::string body_length_str = buffer.substr(body_length_val_pos, body_length_end_pos - body_length_val_pos);
            int body_length = 0;
            try {
                body_length = std::stoi(body_length_str);
                if (body_length < 0 || body_length > 4096) throw std::runtime_error("Invalid BodyLength");
            } catch (const std::exception&) {
                session_->on_io_error("Corrupted BodyLength");
                buffer.clear();
                break;
            }

            const size_t soh_after_body_length_pos = body_length_end_pos + 1;
            const size_t total_msg_len = soh_after_body_length_pos + body_length + 7; // "10=NNN\x01" is 7 chars
            if (buffer.size() < total_msg_len) break;

            const std::string raw_msg = buffer.substr(0, total_msg_len);
            try {
                std::cout << "<<< RECV (" << fd_ << "): " << raw_msg << std::endl;
                FixMessage msg = session_->codec_.decode(raw_msg);
                session_->on_message_received(std::move(msg));
            } catch (const std::exception& e) {
                session_->on_io_error(std::string("Decode error: ") + e.what());
            }
            buffer.erase(0, total_msg_len);
        }
    } else {
        if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            session_->on_io_error("Connection closed or read error.");
        }
    }
}

void Connection::handle_write() {
    session_->send_buffered_data();
    
    // If the outbound queue is now empty, unregister for write events
    if (session_->outbound_q_.empty()) {
        reactor_->modify_fd(fd_, EventType::READ, nullptr);
    }
}

void Connection::send(std::string_view data) {
    ssize_t sent = ::send(fd_, data.data(), data.length(), 0);

    if (sent >= 0) {
        if (static_cast<size_t>(sent) < data.length()) {
            // Partial send
            session_->outbound_q_.enqueue(std::string(data.substr(sent)));
            reactor_->modify_fd(fd_, EventType::READ | EventType::WRITE, [self = shared_from_this()](int) { self->handle_write(); });
        }
        return;
    }

    // sent < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
         // Buffer the whole message and register for write events
        session_->outbound_q_.enqueue(std::string(data));
        reactor_->modify_fd(fd_, EventType::READ | EventType::WRITE, [self = shared_from_this()](int) { self->handle_write(); });
    } else {
        // Real error
        session_->on_io_error(std::string("Send failed: ") + strerror(errno));
    }
}

void Connection::shutdown() {
    std::cout << "Shutting down connection for fd " << fd_ << std::endl;
    reactor_->remove_fd(fd_);
}

} // namespace fix40 