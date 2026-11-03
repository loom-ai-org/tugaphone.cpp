// tugaphone_benchmark: scripts/benchmark.py of the Python repository. The
// repository root is TUGAPHONE_BENCHMARK_ROOT, or the working directory.
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "benchmark.hpp"
#include "pyio.hpp"

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);  // as CPython: a closed pipe is an EPIPE error
    const std::string path = argc > 0 ? argv[0] : "tugaphone_benchmark";
    const std::string prog = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    const char* root = std::getenv("TUGAPHONE_BENCHMARK_ROOT");
    const int status = tugaphone::benchmark::main(prog, std::vector<std::string>(argv + 1, argv + argc),
                                                  root != nullptr && *root != '\0' ? root : ".", std::cout, std::cerr);
    return tugaphone::detail::pyio::finish_stdout(status);
}
