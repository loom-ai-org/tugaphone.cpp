// The tugaphone command line (tugaphone/__main__.py).
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace tugaphone::cli {

// main(argv): parses `args` (argv without the program name), writes the
// result to `out` and messages to `err`, and returns the exit status. A Python
// exception that would end the program with a traceback writes its last line,
// "<class>: <message>", and returns 1.
int main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace tugaphone::cli
