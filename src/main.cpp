// The tugaphone executable: python -m tugaphone.
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "pyio.hpp"

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // as CPython: a closed pipe is an EPIPE error
    const std::vector<std::string> args(argv + 1, argv + argc);
    const int status = tugaphone::cli::main(args, std::cout, std::cerr);
    return tugaphone::detail::pyio::finish_stdout(status);
}
