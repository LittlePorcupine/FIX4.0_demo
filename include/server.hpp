#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>

namespace fix40 {

// Forward declarations
class Reactor;
class ThreadPool;
class TimingWheel;
class Connection;

class FixServer {
public:
    FixServer(int port, int num_threads);
    ~FixServer();

    void start();

private:
    void on_new_connection(int fd);
    void on_connection_close(int fd);

    static void signal_handler(int signum);

    const int port_;
    int listen_fd_;

    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<ThreadPool> worker_pool_;
    std::unique_ptr<TimingWheel> timing_wheel_;

    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;

    static FixServer* instance_for_signal_;
};
} // namespace fix40
