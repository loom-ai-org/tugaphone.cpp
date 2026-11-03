// Byte parity with the Python tugaphone. Each golden record holds the inputs
// and Python's output of one call; this recomputes the record in C++ and
// compares the bytes.
//
// --divergences <file> names the records that are known not to match until
// a dependency fix lands. A listed record that mismatches is counted apart;
// a listed record that matches, or that is not in the goldens, fails the run,
// so that the list shrinks when the fix lands.
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "json_line.hpp"
#include "parity/modules.hpp"

namespace {

struct Tally {
    long expected = 0, checked = 0, mismatches = 0, divergences = 0, stale = 0;
};

// (module, golden line) -> the fix the record waits on.
using Divergences = std::map<std::pair<std::string, std::string>, std::string>;

// One entry per line: module, the golden line, the fix it waits on,
// separated by tabs. A line that starts with '#' is a comment.
Divergences read_divergences(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    Divergences out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t a = line.find('\t'), b = line.rfind('\t');
        if (a == std::string::npos || a == b) throw std::runtime_error("malformed divergence line: " + line);
        out[{line.substr(0, a), line.substr(a + 1, b - a - 1)}] = line.substr(b + 1);
    }
    return out;
}

Tally run(const std::string& dir, const std::string& module, std::string (*record)(const json_line::Value&),
          const Divergences& divergences) {
    const std::string path = dir + "/" + module + ".jsonl.gz";
    const auto lines = json_line::read_gz_lines(path);
    Tally t;
    if (lines.empty()) throw std::runtime_error("empty golden file " + path);
    t.expected = json_line::header_count(lines[0]);
    std::map<std::string, long> by_function;
    std::set<std::string> seen;
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
        const auto listed = divergences.find({module, lines[i]});
        if (listed != divergences.end()) {
            seen.insert(lines[i]);
            if (got == lines[i]) {
                ++t.stale;
                std::cerr << "LISTED DIVERGENCE NOW MATCHES " << module << " (" << listed->second
                          << "): remove it from the list\n  python: " << lines[i].substr(0, 3000) << "\n";
            } else {
                ++t.divergences;
                std::cerr << "EXPECTED DIVERGENCE " << module << " (" << listed->second << ")\n  python: "
                          << lines[i].substr(0, 3000) << "\n  c++:    " << got.substr(0, 3000) << "\n";
            }
            continue;
        }
        if (got == lines[i]) continue;
        ++by_function[v.size() ? v[0].str : "?"];
        if (++t.mismatches <= 10) {
            std::cerr << "MISMATCH " << module << "\n  python: " << lines[i].substr(0, 3000)
                      << "\n  c++:    " << got.substr(0, 3000) << "\n";
        }
    }
    for (const auto& [key, fix] : divergences) {
        if (key.first == module && !seen.count(key.second)) {
            ++t.stale;
            std::cerr << "LISTED DIVERGENCE NOT IN THE GOLDENS " << module << " (" << fix << ")\n  python: "
                      << key.second.substr(0, 3000) << "\n";
        }
    }
    std::cout << module << ": " << t.expected << " in, " << t.checked << " checked, " << t.mismatches
              << " mismatches, " << t.divergences << " expected divergences, " << t.stale << " stale entries\n";
    for (const auto& [fn, n] : by_function) std::cout << "  " << fn << ": " << n << " mismatches\n";
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    Divergences divergences;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--divergences" && i + 1 < args.size()) {
            divergences = read_divergences(args[i + 1]);
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i + 2));
            break;
        }
    }
    if (args.empty()) {
        std::cerr << "usage: tugaphone_parity <goldens dir> [--divergences <file>] [module ...]\n";
        return 2;
    }
    const std::string dir = args[0];
    Tally total;
    auto selected = [&](const std::string& name) {
        if (args.size() == 1) return true;
        for (size_t i = 1; i < args.size(); ++i) {
            if (name == args[i]) return true;
        }
        return false;
    };
#define TUGAPHONE_PARITY_RUN(name)                                      \
    if (selected(#name)) {                                              \
        const Tally t = run(dir, #name, parity::record_##name, divergences); \
        total.expected += t.expected;                                   \
        total.checked += t.checked;                                     \
        total.mismatches += t.mismatches;                               \
        total.divergences += t.divergences;                             \
        total.stale += t.stale;                                         \
    }
    TUGAPHONE_PARITY_MODULES(TUGAPHONE_PARITY_RUN)
#undef TUGAPHONE_PARITY_RUN
    std::cout << "goldens: " << total.expected << " in, " << total.checked << " checked, " << total.mismatches
              << " mismatches, " << total.divergences << " expected divergences, " << total.stale << " stale entries\n";
    return (total.checked == total.expected && total.mismatches == 0 && total.stale == 0) ? 0 : 1;
}
