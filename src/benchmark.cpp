#include "benchmark.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>
#include <climits>

#include "argparse.hpp"
#include "pyio.hpp"
#include "tugaphone/registry.hpp"
#include "tugaphone/tokenizer.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::benchmark {

using namespace detail;

namespace {

constexpr char32_t PRIMARY = U'ˈ';
constexpr char32_t SECONDARY = U'ˌ';
const U32 STRESS_MARKS = U"ˈˌ";
const U32 JOINERS = U"·.|‿͜͡";
const U32 TIES = U"͜͡";
const U32 VOWELS = U"iyɨʉɯuɪʏʊeøɘɵɤoəɛœɜɞ"
                   U"ʌɔæɐaɶɑɒɚɝ";
constexpr char32_t NON_SYLLABIC = U'̯';
constexpr char32_t SYLLABIC[] = {U'̩', U'̍'};

bool has_space(U32View s) { return std::any_of(s.begin(), s.end(), is_space); }

// Python's '%.4f' % x and round(x, 4): the correctly rounded decimal.
std::string fixed4(double x) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.4f", x);
    return buffer;
}
double round4(double x) { return std::strtod(fixed4(x).c_str(), nullptr); }

// json.dumps of a str with ensure_ascii=False.
std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out + "\"";
}

std::string json_num(const std::optional<double>& v) { return v ? float_repr(*v) : "null"; }

// '{:22}' % s: left-aligned in 22 code points.
std::string pad(const std::string& s, size_t width) {
    const size_t n = from_utf8(s).size();
    return n >= width ? s : s + std::string(width - n, ' ');
}

}  // namespace

const std::vector<std::pair<std::string, std::string>>& dialect_to_region() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"pt-PT", "pt-PT"},
        {"pt-PT-x-lisbon", "pt-PT-x-lisboa"},
        {"pt-BR", "pt-BR"},
        {"pt-BR-x-sao-paulo", "pt-BR-x-saopaulo"},
        {"pt-BR-x-rio-janeiro", "pt-BR-x-riodejaneiro"},
        {"pt-AO", "pt-AO"},
        {"pt-MZ", "pt-MZ-x-maputo"},
        {"pt-TL", "pt-TL-x-dili"},
    };
    return table;
}

U32 normalize(U32View ipa) {
    U32 s = nfc(ipa);
    for (char32_t c : STRESS_MARKS + JOINERS) s.erase(std::remove(s.begin(), s.end(), c), s.end());
    U32 out;
    for (const U32& part : split_ws(s)) out += part;
    return out;
}

long levenshtein(U32View a, U32View b) {
    if (a == b) return 0;
    if (a.empty()) return static_cast<long>(b.size());
    if (b.empty()) return static_cast<long>(a.size());
    std::vector<long> prev(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<long>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        std::vector<long> cur{static_cast<long>(i)};
        for (size_t j = 1; j <= b.size(); ++j)
            cur.push_back(std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])}));
        prev = std::move(cur);
    }
    return prev.back();
}

std::pair<long, long> score_word(U32View hyp, const std::vector<U32>& golds) {
    const U32 h = normalize(hyp);
    std::optional<std::pair<long, long>> best;
    for (const U32& gold : golds) {
        const U32 r = normalize(gold);
        const long d = levenshtein(h, r);
        const long len = static_cast<long>(r.size());
        if (!best || static_cast<double>(d) / static_cast<double>(std::max(len, 1L)) <
                         static_cast<double>(best->first) / static_cast<double>(std::max(best->second, 1L)))
            best = std::make_pair(d, len);
    }
    if (!best) throw Error("AssertionError", "");
    return *best;
}

U32 fold_stress(U32View ipa) {
    U32 s = nfd(ipa);
    std::replace(s.begin(), s.end(), U'\'', PRIMARY);
    return s;
}

bool is_nucleus(const U32& seg) {
    for (char32_t mark : SYLLABIC)
        if (seg.find(mark) != U32::npos) return true;
    return VOWELS.find(seg.at(0)) != U32::npos && seg.find(NON_SYLLABIC) == U32::npos;
}

Segments segments(U32View ipa) {
    Segments r;
    long stress_from = 0;
    for (char32_t ch : ipa) {
        if (combining(ch) != 0 || (!r.segs.empty() && TIES.find(r.segs.back().back()) != U32::npos)) {
            if (!r.segs.empty()) r.segs.back().push_back(ch);
        } else if (ch == PRIMARY) {
            ++r.marks;
            stress_from = static_cast<long>(r.segs.size());
        } else if (ch != SECONDARY && JOINERS.find(ch) == U32::npos && !is_space(ch)) {
            r.segs.emplace_back(1, ch);
        }
    }
    if (r.marks != 1) return r;
    for (long i = stress_from; i < static_cast<long>(r.segs.size()); ++i) {
        if (is_nucleus(r.segs[static_cast<size_t>(i)])) {
            r.stressed = i;
            break;
        }
    }
    return r;
}

bool stress_eligible(U32View gold) {
    const U32 folded = fold_stress(strip_ws(gold));
    if (has_space(folded)) return false;
    const Segments s = segments(folded);
    const long nuclei = std::count_if(s.segs.begin(), s.segs.end(), is_nucleus);
    return s.marks == 1 && s.stressed && nuclei >= 2;
}

std::map<long, long> align(const std::vector<U32>& ref, const std::vector<U32>& hyp) {
    using Cost = std::pair<long, long>;
    auto sub = [](const U32& a, const U32& b) -> Cost { return {a != b, is_nucleus(a) != is_nucleus(b)}; };
    auto add = [](Cost c, Cost d) -> Cost { return {c.first + d.first, c.second + d.second}; };
    const Cost gap{1, 0};
    const size_t n = ref.size(), m = hyp.size();
    std::vector<std::vector<Cost>> dp(n + 1, std::vector<Cost>(m + 1, Cost{0, 0}));
    for (size_t i = 1; i <= n; ++i) dp[i][0] = {static_cast<long>(i), 0};
    for (size_t j = 1; j <= m; ++j) dp[0][j] = {static_cast<long>(j), 0};
    for (size_t i = 1; i <= n; ++i) {
        for (size_t j = 1; j <= m; ++j) {
            // min() returns the first of equal candidates.
            Cost best = add(dp[i - 1][j], gap);
            const Cost ins = add(dp[i][j - 1], gap);
            if (ins < best) best = ins;
            const Cost diag = add(dp[i - 1][j - 1], sub(ref[i - 1], hyp[j - 1]));
            if (diag < best) best = diag;
            dp[i][j] = best;
        }
    }
    std::map<long, long> pairs;
    size_t i = n, j = m;
    while (i && j) {
        if (dp[i][j] == add(dp[i - 1][j - 1], sub(ref[i - 1], hyp[j - 1]))) {
            pairs[static_cast<long>(i - 1)] = static_cast<long>(j - 1);
            --i;
            --j;
        } else if (dp[i][j] == add(dp[i - 1][j], gap)) {
            --i;
        } else {
            --j;
        }
    }
    return pairs;
}

bool stress_hit(U32View hyp, U32View gold) {
    const Segments ref = segments(fold_stress(strip_ws(gold)));
    const Segments out = segments(fold_stress(strip_ws(hyp)));
    if (!ref.stressed || !out.stressed) return false;
    const auto pairs = align(ref.segs, out.segs);
    const auto it = pairs.find(*ref.stressed);
    return it != pairs.end() && it->second == *out.stressed;
}

std::optional<bool> score_stress(U32View word, U32View hyp, const std::vector<U32>& golds) {
    std::vector<U32> eligible;
    for (const U32& g : golds)
        if (stress_eligible(g)) eligible.push_back(g);
    if (eligible.empty() || has_space(strip_ws(word))) return std::nullopt;
    return std::any_of(eligible.begin(), eligible.end(), [&](const U32& g) { return stress_hit(hyp, g); });
}

std::optional<double> stress_accuracy(long hits, long n_stress) {
    if (n_stress < STRESS_MIN_WORDS) return std::nullopt;
    return round4(static_cast<double>(hits) / static_cast<double>(n_stress));
}

// ── difflib.SequenceMatcher ─────────────────────────────────────────────────

namespace {

struct Matcher {
    U32View a, b;
    std::map<char32_t, std::vector<long>> b2j;

    Matcher(U32View a_, U32View b_) : a(a_), b(b_) {
        for (size_t i = 0; i < b.size(); ++i) b2j[b[i]].push_back(static_cast<long>(i));
        // autojunk: in a sequence of 200 or more, an element that fills more
        // than 1% is popular and is left out of b2j.
        const long n = static_cast<long>(b.size());
        if (n >= 200) {
            const size_t ntest = static_cast<size_t>(n / 100 + 1);
            for (auto it = b2j.begin(); it != b2j.end();) {
                if (it->second.size() > ntest) it = b2j.erase(it);
                else ++it;
            }
        }
    }

    std::tuple<long, long, long> find_longest_match(long alo, long ahi, long blo, long bhi) const {
        long besti = alo, bestj = blo, bestsize = 0;
        std::map<long, long> j2len;
        for (long i = alo; i < ahi; ++i) {
            std::map<long, long> newj2len;
            const auto it = b2j.find(a[static_cast<size_t>(i)]);
            if (it != b2j.end()) {
                for (long j : it->second) {
                    if (j < blo) continue;
                    if (j >= bhi) break;
                    const auto prev = j2len.find(j - 1);
                    const long k = newj2len[j] = (prev == j2len.end() ? 0 : prev->second) + 1;
                    if (k > bestsize) {
                        besti = i - k + 1;
                        bestj = j - k + 1;
                        bestsize = k;
                    }
                }
            }
            j2len = std::move(newj2len);
        }
        while (besti > alo && bestj > blo && a[static_cast<size_t>(besti - 1)] == b[static_cast<size_t>(bestj - 1)]) {
            --besti;
            --bestj;
            ++bestsize;
        }
        while (besti + bestsize < ahi && bestj + bestsize < bhi &&
               a[static_cast<size_t>(besti + bestsize)] == b[static_cast<size_t>(bestj + bestsize)])
            ++bestsize;
        return {besti, bestj, bestsize};
    }

    std::vector<std::tuple<long, long, long>> matching_blocks() const {
        const long la = static_cast<long>(a.size()), lb = static_cast<long>(b.size());
        std::vector<std::tuple<long, long, long, long>> queue{{0, la, 0, lb}};
        std::vector<std::tuple<long, long, long>> blocks;
        while (!queue.empty()) {
            const auto [alo, ahi, blo, bhi] = queue.back();
            queue.pop_back();
            const auto x = find_longest_match(alo, ahi, blo, bhi);
            const auto [i, j, k] = x;
            if (k) {
                blocks.push_back(x);
                if (alo < i && blo < j) queue.emplace_back(alo, i, blo, j);
                if (i + k < ahi && j + k < bhi) queue.emplace_back(i + k, ahi, j + k, bhi);
            }
        }
        std::sort(blocks.begin(), blocks.end());
        long i1 = 0, j1 = 0, k1 = 0;
        std::vector<std::tuple<long, long, long>> out;
        for (const auto& [i2, j2, k2] : blocks) {
            if (i1 + k1 == i2 && j1 + k1 == j2) {
                k1 += k2;
            } else {
                if (k1) out.emplace_back(i1, j1, k1);
                i1 = i2;
                j1 = j2;
                k1 = k2;
            }
        }
        if (k1) out.emplace_back(i1, j1, k1);
        out.emplace_back(la, lb, 0);
        return out;
    }
};

}  // namespace

std::vector<Opcode> opcodes(U32View a, U32View b) {
    const Matcher m(a, b);
    long i = 0, j = 0;
    std::vector<Opcode> answer;
    for (const auto& [ai, bj, size] : m.matching_blocks()) {
        std::string tag;
        if (i < ai && j < bj) tag = "replace";
        else if (i < ai) tag = "delete";
        else if (j < bj) tag = "insert";
        if (!tag.empty()) answer.push_back({tag, i, ai, j, bj});
        i = ai + size;
        j = bj + size;
        if (size) answer.push_back({"equal", ai, i, bj, j});
    }
    return answer;
}

void Confusions::add(const U32& got, const U32& expected) {
    const auto key = std::make_pair(got, expected);
    const auto it = index.find(key);
    if (it == index.end()) {
        index.emplace(key, items.size());
        items.emplace_back(key, 1);
    } else {
        ++items[it->second].second;
    }
}

void count_confusions(U32View hyp, const std::vector<U32>& golds, Confusions& confusion) {
    const U32 h = normalize(hyp);
    std::optional<U32> r;
    double best = 0;
    for (const U32& g : golds) {
        U32 candidate = normalize(g);
        const double key = static_cast<double>(levenshtein(h, candidate)) /
                           static_cast<double>(std::max(static_cast<long>(candidate.size()), 1L));
        if (!r || key < best) {
            r = std::move(candidate);
            best = key;
        }
    }
    if (!r) throw ValueError("min() iterable argument is empty");
    for (const Opcode& op : opcodes(h, *r)) {
        if (op.tag == "equal") continue;
        confusion.add(h.substr(static_cast<size_t>(op.i1), static_cast<size_t>(op.i2 - op.i1)),
                      r->substr(static_cast<size_t>(op.j1), static_cast<size_t>(op.j2 - op.j1)));
    }
}

// ── rules-only phonemization and scoring ────────────────────────────────────

std::string rules_only_ipa(const std::string& word, const std::string& dialect_code) {
    static std::map<std::string, tokenizer::DialectPtr> cache;
    auto it = cache.find(dialect_code);
    if (it == cache.end()) {
        dialects::DialectInventory inv = registry::get_dialect_inventory(dialect_code);
        inv.IRREGULAR_WORDS = {};
        it = cache.emplace(dialect_code, std::make_shared<const dialects::DialectInventory>(std::move(inv))).first;
    }
    const tokenizer::Sentence sentence(word, it->second);
    return to_utf8(strip_ws(from_utf8(sentence.ipa())));
}

std::vector<std::pair<std::string, std::vector<std::string>>> load_fixture(const std::string& path) {
    const std::string text = pyio::read_text(path);
    std::vector<std::pair<std::string, std::vector<std::string>>> golds;
    std::map<std::string, size_t> index;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        const size_t next = end == std::string::npos ? text.size() : end + 1;
        const std::string line = text.substr(start, next - start);
        start = next;
        if (line.rfind('#', 0) == 0 || strip_ws(from_utf8(line)).empty()) continue;
        std::string body = line;
        while (!body.empty() && body.back() == '\n') body.pop_back();
        std::vector<std::string> fields;
        size_t from = 0;
        while (true) {
            const size_t tab = body.find('\t', from);
            fields.push_back(body.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
            if (tab == std::string::npos) break;
            from = tab + 1;
        }
        if (fields.size() < 2)
            throw ValueError("not enough values to unpack (expected 2, got " + std::to_string(fields.size()) + ")");
        if (fields.size() > 2) throw ValueError("too many values to unpack (expected 2)");
        const auto it = index.find(fields[0]);
        if (it == index.end()) {
            index.emplace(fields[0], golds.size());
            golds.push_back({fields[0], {fields[1]}});
        } else {
            golds[it->second].second.push_back(fields[1]);
        }
    }
    return golds;
}

Row score_dialect(const std::string& dialect, const std::string& fixture_path, std::optional<long> limit) {
    const auto fixture = load_fixture(fixture_path);
    std::map<std::string, std::vector<U32>> golds;
    for (const auto& [word, variants] : fixture)
        for (const std::string& v : variants) golds[word].push_back(from_utf8(v));
    std::vector<std::string> words;
    for (const auto& [word, variants] : golds) words.push_back(word);  // sorted(golds)
    if (limit) {
        // words[:limit]
        const long n = static_cast<long>(words.size());
        long stop = *limit < 0 ? std::max(n + *limit, 0L) : std::min(*limit, n);
        words.resize(static_cast<size_t>(stop));
    }
    long err = 0, tot = 0, exact = 0, failed = 0, n_stress = 0, stress_hits = 0;
    Confusions confusion;
    for (const std::string& word : words) {
        std::string hyp;
        try {
            hyp = rules_only_ipa(word, dialect);
        } catch (const std::exception&) {
            ++failed;
            continue;
        }
        const U32 h = from_utf8(hyp), w = from_utf8(word);
        const std::vector<U32>& g = golds[word];
        if (const auto hit = score_stress(w, h, g)) {
            ++n_stress;
            stress_hits += *hit;
        }
        const auto [d, n] = score_word(h, g);
        err += d;
        tot += n;
        if (d == 0) ++exact;
        else count_confusions(h, g, confusion);
    }
    auto items = confusion.items;
    std::stable_sort(items.begin(), items.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    if (items.size() > 20) items.resize(20);
    Row row;
    row.dialect = dialect;
    const auto& table = dialect_to_region();
    const auto region = std::find_if(table.begin(), table.end(), [&](const auto& kv) { return kv.first == dialect; });
    if (region == table.end()) throw KeyError(py_str_repr(from_utf8(dialect)));
    row.region = region->second;
    row.n = static_cast<long>(words.size());
    row.failed = failed;
    if (tot) row.per = round4(static_cast<double>(err) / static_cast<double>(tot));
    if (!words.empty()) row.word_accuracy = round4(static_cast<double>(exact) / static_cast<double>(words.size()));
    row.stress_acc = stress_accuracy(stress_hits, n_stress);
    row.n_stress = n_stress;
    for (const auto& [key, count] : items) row.top_confusions.emplace_back(to_utf8(key.first), to_utf8(key.second), count);
    return row;
}

// ── reports ─────────────────────────────────────────────────────────────────

std::string row_json(const Row& r) {
    std::string out = "{\"dialect\": " + json_str(r.dialect) + ", \"region\": " + json_str(r.region) +
                      ", \"n\": " + std::to_string(r.n) + ", \"failed\": " + std::to_string(r.failed) +
                      ", \"per\": " + json_num(r.per) + ", \"word_accuracy\": " + json_num(r.word_accuracy) +
                      ", \"stress_acc\": " + json_num(r.stress_acc) + ", \"n_stress\": " + std::to_string(r.n_stress) +
                      ", \"top_confusions\": [";
    for (size_t i = 0; i < r.top_confusions.size(); ++i) {
        const auto& [got, expected, count] = r.top_confusions[i];
        out += (i ? ", " : "") + std::string("{\"got\": ") + json_str(got) + ", \"expected\": " + json_str(expected) +
               ", \"count\": " + std::to_string(count) + "}";
    }
    return out + "], \"harness_version\": " + json_str(HARNESS_VERSION) + "}";
}

std::string results_json(const std::vector<Row>& rows) {
    if (rows.empty()) return "[]\n";
    std::string out = "[\n";
    for (size_t k = 0; k < rows.size(); ++k) {
        const Row& r = rows[k];
        out += "  {\n";
        out += "    \"dialect\": " + json_str(r.dialect) + ",\n";
        out += "    \"region\": " + json_str(r.region) + ",\n";
        out += "    \"n\": " + std::to_string(r.n) + ",\n";
        out += "    \"failed\": " + std::to_string(r.failed) + ",\n";
        out += "    \"per\": " + json_num(r.per) + ",\n";
        out += "    \"word_accuracy\": " + json_num(r.word_accuracy) + ",\n";
        out += "    \"stress_acc\": " + json_num(r.stress_acc) + ",\n";
        out += "    \"n_stress\": " + std::to_string(r.n_stress) + ",\n";
        if (r.top_confusions.empty()) {
            out += "    \"top_confusions\": [],\n";
        } else {
            out += "    \"top_confusions\": [\n";
            for (size_t i = 0; i < r.top_confusions.size(); ++i) {
                const auto& [got, expected, count] = r.top_confusions[i];
                out += "      {\n        \"got\": " + json_str(got) + ",\n        \"expected\": " + json_str(expected) +
                       ",\n        \"count\": " + std::to_string(count) + "\n      }";
                out += i + 1 < r.top_confusions.size() ? ",\n" : "\n";
            }
            out += "    ],\n";
        }
        out += "    \"harness_version\": " + json_str(HARNESS_VERSION) + "\n";
        out += k + 1 < rows.size() ? "  },\n" : "  }\n";
    }
    return out + "]\n";
}

std::string scoreboard_md(const std::vector<Row>& rows) {
    std::vector<std::string> lines = {
        "# Scoreboard — rules-only PER and stress accuracy per dialect",
        "",
        "Generated by `python scripts/benchmark.py` from the committed gold",
        "fixtures (`tests/data/gold/`), which are fixed-seed samples of",
        "[TigreGotico/portuguese-unified-pronunciation-lexicon]"
        "(https://huggingface.co/datasets/TigreGotico/portuguese-unified-pronunciation-lexicon).",
        "Rules-only: the lexicon exception path is emptied, so the number",
        "measures grapheme-rule quality, not lexicon lookup coverage — see",
        "[benchmarking.md](benchmarking.md).",
        "",
        "Stress accuracy is the share of the stress-eligible gold words",
        "(one word, one primary mark, two syllables or more) whose primary",
        "stress lands on the gold's syllable. PER cannot see that choice.",
        "",
        "| Dialect | Gold region | Words | PER | Word accuracy | Stress words | Stress accuracy |",
        "|---|---|---|---|---|---|---|",
    };
    auto fmt = [](const std::optional<double>& v) {
        if (!v) throw TypeError("unsupported format string passed to NoneType.__format__");
        return fixed4(*v);
    };
    for (const Row& r : rows) {
        const std::string stress = r.stress_acc ? fixed4(*r.stress_acc) : "—";
        lines.push_back("| " + r.dialect + " | " + r.region + " | " + std::to_string(r.n) + " | " + fmt(r.per) +
                        " | " + fmt(r.word_accuracy) + " | " + std::to_string(r.n_stress) + " | " + stress + " |");
    }
    lines.emplace_back("");
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
    return out;
}

std::string summary_line(const Row& row) {
    auto fmt = [](const std::optional<double>& v) {
        if (!v) throw TypeError("unsupported format string passed to NoneType.__format__");
        return fixed4(*v);
    };
    const std::string stress = row.stress_acc ? fixed4(*row.stress_acc) : "-";
    std::string n = std::to_string(row.n);
    if (n.size() < 5) n = std::string(5 - n.size(), ' ') + n;
    return pad(row.dialect, 22) + " n=" + n + " PER=" + fmt(row.per) + " acc=" + fmt(row.word_accuracy) +
           " stress=" + stress + " (n=" + std::to_string(row.n_stress) + ")";
}

int main(const std::string& prog, const std::vector<std::string>& args, const std::string& root, std::ostream& out,
         std::ostream& err) {
    namespace ap = argparse;
    ap::Parser parser(u(prog), U32(U"Gold-benchmark harness: rules-only PER and stress accuracy per dialect."));
    ap::Action dialect;
    dialect.help = U"Score one dialect code only.";
    parser.add_argument({U"--dialect"}, std::move(dialect));
    ap::Action limit;
    limit.help = U"Cap words per dialect (ad-hoc runs).";
    limit.int_type = true;
    parser.add_argument({U"--limit"}, std::move(limit));
    ap::Action refresh;
    refresh.kind = ap::Action::Kind::StoreTrue;
    refresh.help = U"Re-download the gold and rewrite the committed fixtures (network; maintainer action).";
    parser.add_argument({U"--refresh-fixtures"}, std::move(refresh));

    std::vector<U32> argv;
    for (const std::string& a : args) argv.push_back(pyio::fsdecode(a));
    try {
        const ap::Namespace ns = parser.parse_args(argv, out, err);
        auto get = [&](const char32_t* key) -> std::optional<std::string> {
            const auto it = ns.find(key);
            if (it == ns.end() || !it->second) return std::nullopt;
            return pyio::fsencode(*it->second);
        };
        if (get(U"refresh_fixtures") == std::optional<std::string>("True")) {
            err << "--refresh-fixtures downloads the gold from the network; the C++ port does not do this\n";
            return 2;
        }
        const std::string dir = pyio::path_join(root, "tests/data/gold");
        std::vector<std::string> dialects;
        const auto chosen = get(U"dialect");
        if (chosen && !chosen->empty()) {
            dialects.push_back(*chosen);
        } else {
            for (const auto& [d, region] : dialect_to_region()) dialects.push_back(d);
            std::sort(dialects.begin(), dialects.end());
        }
        std::optional<long> cap;
        if (const auto l = get(U"limit")) {
            try {
                cap = std::stol(*l);
            } catch (const std::out_of_range&) {
                cap = (*l)[0] == '-' ? LONG_MIN : LONG_MAX;
            }
        }
        std::vector<Row> rows;
        for (const std::string& d : dialects) {
            Row row = score_dialect(d, pyio::path_join(dir, d + ".tsv"), cap);
            out << summary_line(row) << "\n";
            rows.push_back(std::move(row));
        }
        if (!(chosen && !chosen->empty()) && !cap) {
            const std::string results_dir = pyio::path_join(root, "benchmarks");
            const std::string results_path = pyio::path_join(results_dir, "results.json");
            pyio::makedirs(results_dir);
            pyio::write_text(results_path, results_json(rows));
            pyio::write_text(pyio::path_join(root, "docs/scoreboard.md"), scoreboard_md(rows));
        }
        return 0;
    } catch (const ap::Exit& e) {
        return e.status;
    } catch (...) {
        return pyio::print_exception(out, err);
    }
}

}  // namespace tugaphone::benchmark
