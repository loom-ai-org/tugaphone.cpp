// The tugaphone executable: python -m tugaphone.
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const int status = tugaphone::cli::main(args, std::cout, std::cerr);
    std::cout.flush();
    return status;
}
