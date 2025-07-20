#pragma once

#include <string>
#include <memory>
#include <thread>

namespace fix40 {

class Connection;
class Session;
class Reactor;
class ThreadPool;
class TimingWheel;

class Client {
public:
    Client();
    ~Client();

    bool connect(const std::string& ip, int port);
    void disconnect();
    void run_console();

private:
    void on_connection_close();

    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<ThreadPool> worker_pool_;
    std::unique_ptr<TimingWheel> timing_wheel_;
    
    std::shared_ptr<Connection> connection_;
    std::shared_ptr<Session> session_;

    std::thread reactor_thread_;
};

} // namespace fix40 