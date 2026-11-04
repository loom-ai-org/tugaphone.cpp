// scripts/benchmark.py of tugaphone: rules-only PER and stress accuracy per
// dialect, scored against the gold fixtures. Same scoring, same rounding,
// same reports, byte for byte.
#pragma once

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "detail.hpp"

namespace tugaphone::benchmark {

using detail::U32;
using detail::U32View;

// DIALECT_TO_REGION, in the order of the Python dict.
const std::vector<std::pair<std::string, std::string>>& dialect_to_region();
inline constexpr long FIXTURE_WORDS = 1000;
inline constexpr long SAMPLE_SEED = 20260713;
inline constexpr const char* HARNESS_VERSION = "1.0";
inline constexpr long STRESS_MIN_WORDS = 20;

// normalize(ipa): NFC, no stress marks or joiners, no whitespace.
U32 normalize(U32View ipa);
// _levenshtein(a, b).
long levenshtein(U32View a, U32View b);
// score_word(hyp, golds): (edit distance, reference length) against the
// closest gold variant.
std::pair<long, long> score_word(U32View hyp, const std::vector<U32>& golds);

// _segments(ipa): the segments, the index of the stressed nucleus and the
// number of primary marks.
struct Segments {
    std::vector<U32> segs;
    std::optional<long> stressed;
    long marks = 0;
};
U32 fold_stress(U32View ipa);
Segments segments(U32View ipa);
bool is_nucleus(const U32& seg);
bool stress_eligible(U32View gold);
// _align(ref, hyp): ref index -> hyp index.
std::map<long, long> align(const std::vector<U32>& ref, const std::vector<U32>& hyp);
bool stress_hit(U32View hyp, U32View gold);
std::optional<bool> score_stress(U32View word, U32View hyp, const std::vector<U32>& golds);
std::optional<double> stress_accuracy(long hits, long n_stress);

// difflib.SequenceMatcher(None, a, b).get_opcodes().
struct Opcode {
    std::string tag;
    long i1, i2, j1, j2;
};
std::vector<Opcode> opcodes(U32View a, U32View b);

// The confusion counts in insertion order, as the Python dict keeps them.
struct Confusions {
    std::vector<std::pair<std::pair<U32, U32>, long>> items;
    std::map<std::pair<U32, U32>, size_t> index;
    void add(const U32& got, const U32& expected);
};
void count_confusions(U32View hyp, const std::vector<U32>& golds, Confusions& confusion);

// rules_only_ipa(word, dialect_code): the tokenizer with the dialect's
// lexicon exception table emptied.
std::string rules_only_ipa(const std::string& word, const std::string& dialect_code);

// load_fixture(dialect): word -> attested variants, in file order.
std::vector<std::pair<std::string, std::vector<std::string>>> load_fixture(const std::string& path);

// One row of results.json.
struct Row {
    std::string dialect, region;
    long n = 0, failed = 0, n_stress = 0;
    std::optional<double> per, word_accuracy, stress_acc;
    std::vector<std::tuple<std::string, std::string, long>> top_confusions;
};
Row score_dialect(const std::string& dialect, const std::string& fixture_path, std::optional<long> limit);

// json.dumps(row, ensure_ascii=False) with the default separators.
std::string row_json(const Row& row);
// The bytes write_reports writes to benchmarks/results.json and docs/scoreboard.md.
std::string results_json(const std::vector<Row>& rows);
std::string scoreboard_md(const std::vector<Row>& rows);
// The line main() prints for a row.
std::string summary_line(const Row& row);

// main(): `args` without the program name. `root` stands for the repository
// root of the Python script: the gold fixtures are read from
// root/tests/data/gold, and the reports go to root/benchmarks/results.json
// and root/docs/scoreboard.md.
int main(const std::string& prog, const std::vector<std::string>& args, const std::string& root, std::ostream& out,
         std::ostream& err);

}  // namespace tugaphone::benchmark
