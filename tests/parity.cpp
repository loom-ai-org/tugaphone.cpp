// Byte parity with the Python tugaphone. Each golden record holds the inputs
// and Python's output of one call; this recomputes the record in C++ and
// compares the bytes.
//
// --divergences <file> names the records that are known not to match until
// a dependency fix lands. A listed record that mismatches is counted apart;
// a listed record that matches, or that is not in the goldens, fails the run,
// so that the list shrinks when the fix lands.
//
// --shard k/n checks the records whose index modulo n is k, so that ctest can
// run the large modules in parallel processes. --mismatches <file> writes each
// golden line that mismatches, in full, to the file.
#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <pthread.h>
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
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t a = line.find('\t'), b = line.rfind('\t');
        if (std::count(line.begin(), line.end(), '\t') != 2)
            throw std::runtime_error("divergence line does not have three tab-separated fields: " + line);
        out[{line.substr(0, a), line.substr(a + 1, b - a - 1)}] = line.substr(b + 1);
    }
    return out;
}

struct Shard {
    long index = 0, count = 1;
};

Tally run(const std::string& dir, const std::string& module, std::string (*record)(const json_line::Value&),
          const Divergences& divergences, long shown, const Shard& shard, std::ostream* mismatches) {
    const std::string path = dir + "/" + module + ".jsonl.gz";
    const auto lines = json_line::read_gz_lines(path);
    Tally t;
    if (lines.empty()) throw std::runtime_error("empty golden file " + path);
    const long total = json_line::header_count(lines[0]);
    t.expected = shard.index < total ? (total - shard.index + shard.count - 1) / shard.count : 0;
    std::map<std::string, long> by_function;
    // The listed lines the whole file holds: a listed divergence is stale only
    // when the file lacks it, whichever shard runs.
    std::set<std::string> seen;
    for (const auto& [key, fix] : divergences)
        if (key.first == module) seen.insert(key.second);
    std::set<std::string> present;
    for (size_t i = 1; i < lines.size(); ++i)
        if (seen.count(lines[i])) present.insert(lines[i]);
    for (size_t i = 1; i < lines.size(); ++i) {
        if (static_cast<long>(i - 1) % shard.count != shard.index) continue;
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
        if (mismatches != nullptr) *mismatches << lines[i] << "\n";
        ++by_function[v.size() ? v[0].str : "?"];
        if (++t.mismatches <= shown) {
            std::cerr << "MISMATCH " << module << "\n  python: " << lines[i].substr(0, 3000)
                      << "\n  c++:    " << got.substr(0, 3000) << "\n";
        }
    }
    for (const auto& [key, fix] : divergences) {
        if (key.first == module && !present.count(key.second)) {
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

const std::vector<std::string>& module_names() {
#define TUGAPHONE_PARITY_NAME(name) #name,
    static const std::vector<std::string> names = {TUGAPHONE_PARITY_MODULES(TUGAPHONE_PARITY_NAME)};
#undef TUGAPHONE_PARITY_NAME
    return names;
}

struct Options {
    std::string dir;
    std::vector<std::string> modules;  // empty: every module
    Divergences divergences;
    long shown = 10;  // mismatches printed per module
    Shard shard;
    std::string mismatches;  // empty: do not write them
};

int run_all(const Options& opt) {
    for (const auto& [key, fix] : opt.divergences) {
        const auto& names = module_names();
        if (std::find(names.begin(), names.end(), key.first) == names.end())
            throw std::runtime_error("divergence row names an unknown module \"" + key.first + "\" (" + fix + ")");
    }
    auto selected = [&](const std::string& name) {
        return opt.modules.empty() || std::find(opt.modules.begin(), opt.modules.end(), name) != opt.modules.end();
    };
    std::ofstream mismatch_file;
    if (!opt.mismatches.empty()) {
        mismatch_file.open(opt.mismatches);
        if (!mismatch_file) throw std::runtime_error("cannot write " + opt.mismatches);
    }
    std::ostream* mismatches = opt.mismatches.empty() ? nullptr : &mismatch_file;
    Tally total;
#define TUGAPHONE_PARITY_RUN(name)                                                  \
    if (selected(#name)) {                                                          \
        const Tally t = run(opt.dir, #name, parity::record_##name, opt.divergences, opt.shown, opt.shard, mismatches); \
        total.expected += t.expected;                                               \
        total.checked += t.checked;                                                 \
        total.mismatches += t.mismatches;                                           \
        total.divergences += t.divergences;                                         \
        total.stale += t.stale;                                                     \
    }
    TUGAPHONE_PARITY_MODULES(TUGAPHONE_PARITY_RUN)
#undef TUGAPHONE_PARITY_RUN
    std::cout << "goldens: " << total.expected << " in, " << total.checked << " checked, " << total.mismatches
              << " mismatches, " << total.divergences << " expected divergences, " << total.stale << " stale entries\n";
    return (total.checked == total.expected && total.mismatches == 0 && total.stale == 0) ? 0 : 1;
}

int run_guarded(const Options& opt) {
    try {
        return run_all(opt);
    } catch (const std::exception& e) {
        std::cerr << "tugaphone_parity: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace

// usage: tugaphone_parity <goldens dir> [--divergences <file>] [--stack-kb <n>] [--show <n>] [--shard <k>/<n>]
//        [--mismatches <file>] [module ...]
// --stack-kb runs the records on a thread with that stack size, to show that
// the matchers do not need a stack that grows with the subject. --show sets
// how many mismatches each module prints (10 by default).
int main(int argc, char** argv) {
    Options opt;
    size_t stack_kb = 0;
    std::vector<std::string> rest;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if ((a == "--divergences" || a == "--stack-kb" || a == "--show" || a == "--shard" ||
                 a == "--mismatches") &&
                i + 1 < argc) {
                if (a == "--divergences") {
                    opt.divergences = read_divergences(argv[++i]);
                } else if (a == "--mismatches") {
                    opt.mismatches = argv[++i];
                } else if (a == "--stack-kb") {
                    stack_kb = std::stoul(argv[++i]);
                } else if (a == "--show") {
                    opt.shown = std::stol(argv[++i]);
                } else {
                    const std::string spec = argv[++i];
                    const size_t slash = spec.find('/');
                    if (slash == std::string::npos) throw std::runtime_error("--shard takes k/n");
                    opt.shard = {std::stol(spec.substr(0, slash)), std::stol(spec.substr(slash + 1))};
                    if (opt.shard.count < 1 || opt.shard.index < 0 || opt.shard.index >= opt.shard.count)
                        throw std::runtime_error("--shard takes k/n with 0 <= k < n");
                }
            } else {
                rest.push_back(a);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "tugaphone_parity: " << e.what() << "\n";
        return 1;
    }
    if (rest.empty()) {
        std::cerr << "usage: tugaphone_parity <goldens dir> [--divergences <file>] [--stack-kb <n>] [--show <n>] "
                     "[--shard <k>/<n>] [--mismatches <file>] [module ...]\n";
        return 2;
    }
    opt.dir = rest[0];
    opt.modules.assign(rest.begin() + 1, rest.end());
    for (const auto& m : opt.modules) {
        const auto& names = module_names();
        if (std::find(names.begin(), names.end(), m) == names.end()) {
            std::cerr << "tugaphone_parity: unknown module \"" << m << "\"\n";
            return 2;
        }
    }
    if (stack_kb == 0) return run_guarded(opt);
    struct Job {
        const Options* opt;
        int status;
    } job{&opt, 1};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (pthread_attr_setstacksize(&attr, stack_kb * 1024) != 0) {
        std::cerr << "tugaphone_parity: cannot set a stack of " << stack_kb << " KB\n";
        return 2;
    }
    pthread_t thread;
    auto body = [](void* p) -> void* {
        auto* j = static_cast<Job*>(p);
        j->status = run_guarded(*j->opt);
        return nullptr;
    };
    if (pthread_create(&thread, &attr, body, &job) != 0) {
        std::cerr << "tugaphone_parity: cannot start the thread\n";
        return 2;
    }
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    return job.status;
}
