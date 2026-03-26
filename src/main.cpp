#include "wifi_direct/command_handler.h"

int main(int argc, char* argv[]) {
    #ifdef __APPLE__
    std::cout << "Running on macOS" << std::endl;
    #endif
    return wifi_direct::run(argc, argv);
}
