// Byte parity with the Python tugaphone. Each golden record holds the inputs
// and Python's output of one call; this recomputes the record in C++ and
// compares the bytes.
#include <exception>
#include <iostream>
#include <map>
#include <string>

#include "json_line.hpp"
#include "parity/modules.hpp"

namespace {

struct Tally {
    long expected = 0, checked = 0, mismatches = 0;
};

Tally run(const std::string& dir, const std::string& module, std::string (*record)(const json_line::Value&)) {
    const std::string path = dir + "/" + module + ".jsonl.gz";
    const auto lines = json_line::read_gz_lines(path);
    Tally t;
    if (lines.empty()) throw std::runtime_error("empty golden file " + path);
    t.expected = json_line::header_count(lines[0]);
    std::map<std::string, long> by_function;
    for (size_t i = 1; i < lines.size(); ++i) {
        std::string got;
        json_line::Value v;
        try {
            v = json_line::parse(lines[i]);
            got = record(v);
        } catch (const std::exception& e) {
            got = std::string("exception: ") + e.what();
        }
        ++t.checked;
        if (got == lines[i]) continue;
        ++by_function[v.size() ? v[0].str : "?"];
        if (++t.mismatches <= 10) {
            std::cerr << "MISMATCH " << module << "\n  python: " << lines[i].substr(0, 3000)
                      << "\n  c++:    " << got.substr(0, 3000) << "\n";
        }
    }
    std::cout << module << ": " << t.expected << " in, " << t.checked << " checked, " << t.mismatches
              << " mismatches\n";
    for (const auto& [fn, n] : by_function) std::cout << "  " << fn << ": " << n << " mismatches\n";
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: tugaphone_parity <goldens dir> [module ...]\n";
        return 2;
    }
    const std::string dir = argv[1];
    Tally total;
    auto selected = [&](const std::string& name) {
        if (argc == 2) return true;
        for (int i = 2; i < argc; ++i) {
            if (name == argv[i]) return true;
        }
        return false;
    };
#define TUGAPHONE_PARITY_RUN(name)                                      \
    if (selected(#name)) {                                              \
        const Tally t = run(dir, #name, parity::record_##name);         \
        total.expected += t.expected;                                   \
        total.checked += t.checked;                                     \
        total.mismatches += t.mismatches;                               \
    }
    TUGAPHONE_PARITY_MODULES(TUGAPHONE_PARITY_RUN)
#undef TUGAPHONE_PARITY_RUN
    std::cout << "goldens: " << total.expected << " in, " << total.checked << " checked, " << total.mismatches
              << " mismatches\n";
    return (total.checked == total.expected && total.mismatches == 0) ? 0 : 1;
}
