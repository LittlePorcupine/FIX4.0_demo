#include "server.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
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