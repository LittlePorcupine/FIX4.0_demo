#include "client.hpp"
#include <iostream>

int main() {
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
