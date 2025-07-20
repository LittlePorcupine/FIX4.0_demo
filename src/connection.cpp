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
    close_fd();
}

void Connection::handle_read() {
    // Prevent reading from a closed connection
    if (is_closed_) return;

    char read_buf[4096];
    ssize_t bytes_read = ::read(fd_, read_buf, sizeof(read_buf));

    if (bytes_read > 0) {
        read_buffer_.append(read_buf, bytes_read);

        if (read_buffer_.size() > kMaxReadBufferSize) {
            session_->on_io_error("Read buffer overflow");
            return;
        }

        while (!read_buffer_.empty()) {
            std::string& buffer = read_buffer_;
            // A very basic framing logic. A robust implementation would be more complex.
            const auto begin_string_pos = buffer.find("8=FIX.4.0\x01");
            if (begin_string_pos == std::string::npos) {
                // Cannot find a valid start of a message.
                // We can't just clear the buffer as a partial message might be at the end.
                // To prevent memory attacks, we've already checked the total buffer size.
                // Here, we can discard data up to where we might see a new message start.
                if (buffer.length() > 20) { // Keep a small tail
                    buffer.erase(0, buffer.length() - 20);
                }
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
    session_->handle_write_ready();

    // If the outbound queue is now empty, unregister for write events
    if (session_->is_outbound_queue_empty()) {
        reactor_->modify_fd(fd_, EventType::READ, nullptr);
    }
}

void Connection::send(std::string_view data) {
    if (is_closed_) return;

    ssize_t sent = ::send(fd_, data.data(), data.length(), MSG_NOSIGNAL); // Use MSG_NOSIGNAL to prevent SIGPIPE

    if (sent >= 0) {
        if (static_cast<size_t>(sent) < data.length()) {
            // Partial send
            session_->enqueue_raw_for_send(std::string(data.substr(sent)));
            reactor_->modify_fd(fd_, EventType::READ | EventType::WRITE, [self = shared_from_this()](int) { self->handle_write(); });
        }
        return;
    }

    // sent < 0, check errno
    switch (errno) {
        case EAGAIN:
        // case EWOULDBLOCK: // EWOULDBLOCK is often the same as EAGAIN
            // Buffer the whole message and register for write events
            session_->enqueue_raw_for_send(std::string(data));
            reactor_->modify_fd(fd_, EventType::READ | EventType::WRITE, [self = shared_from_this()](int) { self->handle_write(); });
            break;
        case EPIPE:
        case ECONNRESET:
            // These are definitive connection closed errors.
            session_->on_io_error(std::string("Send failed: Connection closed by peer."));
            break;
        default:
            // Other real errors
            session_->on_io_error(std::string("Send failed: ") + strerror(errno));
            break;
    }
}

void Connection::shutdown() {
    std::cout << "Shutting down connection for fd " << fd_ << std::endl;
    // Session is responsible for initiating the shutdown by calling this.
    // This method's responsibility is to remove the fd from IO monitoring
    // and close it.
    if (reactor_ && !is_closed_) {
        reactor_->remove_fd(fd_);
    }
    // Final cleanup in close_fd()
    close_fd();
}

void Connection::close_fd() {
    if (!is_closed_.exchange(true)) {
        // First, signal that we will not send or receive any more data.
        // This is the most robust way to close a connection.
        ::shutdown(fd_, SHUT_RDWR);

        // Then, close the file descriptor.
        close(fd_);
    }
}

} // namespace fix40