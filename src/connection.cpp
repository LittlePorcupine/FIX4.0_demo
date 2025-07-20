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

    // ET mode requires us to read until the socket buffer is empty
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
            // Connection closed by peer
            session_->on_io_error("Connection closed by peer.");
            return;
        } else { // bytes_read < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data to read for now
                break;
            } else {
                // A real error occurred
                session_->on_io_error("Socket read error.");
                return;
            }
        }
    }

    // Now, process the accumulated buffer
    while (!read_buffer_.empty()) {
        std::string& buffer = read_buffer_;
        
        // --- FIX Message Framing Logic ---
        const auto begin_string_pos = buffer.find("8=FIX.4.0\x01");
        if (begin_string_pos == std::string::npos) {
            // No valid start found. To prevent buffer bloat with garbage data,
            // we'll clear it. A more robust system might have a different strategy.
            buffer.clear();
            break; 
        }
        if (begin_string_pos > 0) {
            // Discard garbage data before the start of the message
            buffer.erase(0, begin_string_pos);
        }

        const auto body_length_tag_pos = buffer.find("\x01""9=");
        if (body_length_tag_pos == std::string::npos) break; // Not enough data for BodyLength tag

        const auto body_length_val_pos = body_length_tag_pos + 3;
        const auto body_length_end_pos = buffer.find('\x01', body_length_val_pos);
        if (body_length_end_pos == std::string::npos) break; // Not enough data for BodyLength value

        int body_length = 0;
        try {
            const std::string body_length_str = buffer.substr(body_length_val_pos, body_length_end_pos - body_length_val_pos);
            body_length = std::stoi(body_length_str);
            if (body_length < 0 || body_length > 4096) { // Basic sanity check
                throw std::runtime_error("Invalid BodyLength value");
            }
        } catch (const std::exception&) {
            session_->on_io_error("Corrupted or invalid BodyLength");
            buffer.clear(); // Clear buffer to prevent loop on bad data
            break;
        }
        
        // Calculate the total expected message length
        const size_t soh_after_body_length_pos = body_length_end_pos + 1;
        const size_t total_msg_len = soh_after_body_length_pos + body_length + 7; // "10=NNN\x01" is 7 chars

        if (buffer.size() < total_msg_len) {
            // Not enough data for a full message yet
            break;
        }

        // We have a full message, extract and process it
        const std::string raw_msg = buffer.substr(0, total_msg_len);
        try {
            std::cout << "<<< RECV (" << fd_ << "): " << raw_msg << std::endl;
            FixMessage msg = session_->codec_.decode(raw_msg);
            session_->on_message_received(std::move(msg));
        } catch (const std::exception& e) {
            session_->on_io_error(std::string("Decode error: ") + e.what());
        }
        
        // Remove the processed message from the buffer
        buffer.erase(0, total_msg_len);
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

    // The data might not be fully sent, so we'd need a write buffer.
    // For simplicity, this example tries to send it all at once.
    // A robust implementation would buffer the data and use handle_write.
#ifdef __linux__
    ssize_t sent = ::send(fd_, data.data(), data.length(), MSG_NOSIGNAL);
#else
    ssize_t sent = ::send(fd_, data.data(), data.length(), 0);
#endif
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            session_->on_io_error("Send error");
        }
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
