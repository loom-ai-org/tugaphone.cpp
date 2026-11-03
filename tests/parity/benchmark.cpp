// The benchmark records. TUGAPHONE_BENCHMARK_ROOT names the source tree, which
// holds tests/data/gold. A main record runs in a scratch root whose tests
// directory links to the source tree, as the Python dumper does.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <unistd.h>

#include "benchmark.hpp"
#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace {

namespace tb = tugaphone::benchmark;
using tugaphone::detail::from_utf8;
using tugaphone::detail::to_utf8;
using tugaphone::detail::U32;

std::string root() {
    const char* value = std::getenv("TUGAPHONE_BENCHMARK_ROOT");
    if (value == nullptr || *value == '\0') throw std::runtime_error("TUGAPHONE_BENCHMARK_ROOT is not set");
    return value;
}

std::vector<U32> u32s(const Value& v) {
    std::vector<U32> out;
    for (const Value& item : v.items) out.push_back(from_utf8(item.str));
    return out;
}

std::string JU(const U32& s) { return J(to_utf8(s)); }

std::string confusions(const tb::Confusions& c) {
    std::vector<std::string> parts;
    for (const auto& [key, count] : c.items) parts.push_back(list({JU(key.first), JU(key.second), J(count)}));
    return list(parts);
}

std::string stress(const std::optional<bool>& hit) { return hit ? J(*hit) : "null"; }

std::string read_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return "null";
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return J(text.str());
}

// The class in an error line: "TypeError: ..." -> "TypeError".
std::string exception_class(const std::string& text) { return text.substr(0, text.find(':')); }

std::string run_main(const Value& v) {
    std::string pattern = (std::filesystem::temp_directory_path() / "tugaphone-bench-XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
    const std::filesystem::path scratch = pattern;
    std::filesystem::create_directory_symlink(std::filesystem::path(root()) / "tests", scratch / "tests");
    std::filesystem::create_directory(scratch / "docs");
    if (v[2].is_null())
        unsetenv("COLUMNS");
    else
        setenv("COLUMNS", v[2].str.c_str(), 1);
    std::ostringstream out, err;
    const int status = tb::main("benchmark.py", v[1].strings(), scratch.string(), out, err);
    unsetenv("COLUMNS");
    const std::string results = read_file(scratch / "benchmarks" / "results.json");
    const std::string scoreboard = read_file(scratch / "docs" / "scoreboard.md");
    std::filesystem::remove_all(scratch);
    std::string error = err.str();
    // A traceback names Python files; the C++ error line must name the same class.
    const bool traceback = v[8].boolean;
    if (traceback && exception_class(error) == v[4].str) error = v[4].str;
    return list({J("main"), json_line::dump(v[1]), json_line::dump(v[2]), J(out.str()), J(error), J(status),
                 results, scoreboard, json_line::dump(v[8])});
}

}  // namespace

std::string record_benchmark(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "word") {
        return head(v) + guard([&] {
                   const U32 hyp = from_utf8(tb::rules_only_ipa(v[2].str, v[1].str));
                   const std::vector<U32> golds = u32s(v[3]);
                   const auto [d, n] = tb::score_word(hyp, golds);
                   tb::Confusions c;
                   tb::count_confusions(hyp, golds, c);
                   return list({JU(hyp), list({J(d), J(n)}), stress(tb::score_stress(from_utf8(v[2].str), hyp, golds)),
                                confusions(c)});
               }) + "]";
    }
    if (fn == "normalize") return head(v) + guard([&] { return JU(tb::normalize(from_utf8(v[1].str))); }) + "]";
    if (fn == "segments") {
        return head(v) + guard([&] {
                   const tb::Segments s = tb::segments(from_utf8(v[1].str));
                   std::vector<std::string> segs;
                   for (const U32& seg : s.segs) segs.push_back(to_utf8(seg));
                   return list({J(segs), J(s.stressed), J(s.marks)});
               }) + "]";
    }
    if (fn == "stress_eligible") return head(v) + guard([&] { return J(tb::stress_eligible(from_utf8(v[1].str))); }) + "]";
    if (fn == "levenshtein")
        return head(v) + guard([&] { return J(tb::levenshtein(from_utf8(v[1].str), from_utf8(v[2].str))); }) + "]";
    if (fn == "opcodes") {
        return head(v) + guard([&] {
                   std::vector<std::string> ops;
                   for (const tb::Opcode& op : tb::opcodes(from_utf8(v[1].str), from_utf8(v[2].str)))
                       ops.push_back(list({J(op.tag), J(op.i1), J(op.i2), J(op.j1), J(op.j2)}));
                   return list(ops);
               }) + "]";
    }
    if (fn == "stress_hit")
        return head(v) + guard([&] { return J(tb::stress_hit(from_utf8(v[1].str), from_utf8(v[2].str))); }) + "]";
    if (fn == "align") {
        return head(v) + guard([&] {
                   std::vector<std::string> pairs;
                   const auto ref = tb::segments(from_utf8(v[1].str)).segs;
                   const auto hyp = tb::segments(from_utf8(v[2].str)).segs;
                   for (const auto& [i, j] : tb::align(ref, hyp)) pairs.push_back(list({J(i), J(j)}));
                   return list(pairs);
               }) + "]";
    }
    if (fn == "score_word") {
        return head(v) + guard([&] {
                   const auto [d, n] = tb::score_word(from_utf8(v[1].str), u32s(v[2]));
                   return list({J(d), J(n)});
               }) + "]";
    }
    if (fn == "score_stress") {
        return head(v) + guard([&] {
                   return stress(tb::score_stress(from_utf8(v[1].str), from_utf8(v[2].str), u32s(v[3])));
               }) + "]";
    }
    if (fn == "confusions") {
        return head(v) + guard([&] {
                   tb::Confusions c;
                   tb::count_confusions(from_utf8(v[1].str), u32s(v[2]), c);
                   return confusions(c);
               }) + "]";
    }
    if (fn == "stress_accuracy") {
        const auto acc = tb::stress_accuracy(static_cast<long>(v[1].integer), static_cast<long>(v[2].integer));
        return head(v) + (acc ? F(*acc) : "null") + "]";
    }
    if (fn == "DIALECT_TO_REGION") {
        std::vector<std::string> pairs;
        for (const auto& [d, r] : tb::dialect_to_region()) pairs.push_back(list({J(d), J(r)}));
        return list({J(fn), list(pairs)});
    }
    if (fn == "constants") {
        return list({J(fn), list({J(tb::FIXTURE_WORDS), J(tb::SAMPLE_SEED), J(tb::HARNESS_VERSION),
                                  J(tb::STRESS_MIN_WORDS)})});
    }
    if (fn == "score_dialect") {
        return head(v) + guard([&] {
                   std::optional<long> limit;
                   if (!v[2].is_null()) limit = static_cast<long>(v[2].integer);
                   const std::string path = root() + "/tests/data/gold/" + v[1].str + ".tsv";
                   return J(tb::row_json(tb::score_dialect(v[1].str, path, limit)));
               }) + "]";
    }
    if (fn == "main") return run_main(v);
    return "unknown record " + fn;
}

}  // namespace parity
