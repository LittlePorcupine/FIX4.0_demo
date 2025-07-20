#include "client.hpp"
#include <iostream>
#include <csignal>

int main() {
    // Globally ignore the SIGPIPE signal.
    // This is a common practice in networking applications on Unix-like systems.
    signal(SIGPIPE, SIG_IGN);

    try {
        fix40::Client client;
        if (client.connect("127.0.0.1", 9000)) {
            client.run_console();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error in client: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
