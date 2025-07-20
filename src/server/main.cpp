#include "server.hpp"
#include <iostream>
#include <csignal>

int main(int argc, char* argv[]) {
    // Globally ignore the SIGPIPE signal.
    // This is a common practice in networking applications on Unix-like systems.
    // When writing to a socket whose read-end has been closed, this prevents
    // the program from terminating, and instead `send()` will return -1 with errno=EPIPE.
    signal(SIGPIPE, SIG_IGN);

    try {
        // You can customize the number of worker threads via command line,
        // otherwise it defaults to the number of hardware cores.
        int num_threads = (argc > 1) ? std::stoi(argv[1]) : 0;

        fix40::FixServer server(9000, num_threads);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
