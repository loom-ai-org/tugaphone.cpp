#include "unicode_norm.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>

namespace tugaphone::detail {

namespace {
#include "unicode_norm_tables.inc"

constexpr char32_t kSBase = 0xAC00, kLBase = 0x1100, kVBase = 0x1161, kTBase = 0x11A7;
constexpr char32_t kLCount = 19, kVCount = 21, kTCount = 28;
constexpr char32_t kNCount = kVCount * kTCount, kSCount = kLCount * kNCount;

bool is_hangul(char32_t c) { return c >= kSBase && c < kSBase + kSCount; }

template <class Row, size_t N>
const Row* find_row(const Row (&table)[N], char32_t c) {
    const auto* end = std::end(table);
    const auto* it =
        std::lower_bound(std::begin(table), end, c, [](const Row& row, char32_t v) { return row[0] < v; });
    return (it != end && (*it)[0] == c) ? it : nullptr;
}

template <size_t N>
bool in_ranges(const char32_t (&table)[N][2], char32_t c) {
    const auto* end = std::end(table);
    const auto* it = std::upper_bound(std::begin(table), end, c,
                                      [](char32_t v, const char32_t (&row)[2]) { return v < row[0]; });
    return it != std::begin(table) && c <= (*(it - 1))[1];
}

void decompose_into(U32& out, char32_t c) {
    if (is_hangul(c)) {
        const char32_t s = c - kSBase;
        out.push_back(kLBase + s / kNCount);
        out.push_back(kVBase + (s % kNCount) / kTCount);
        if (s % kTCount) out.push_back(kTBase + s % kTCount);
        return;
    }
    if (const auto* row = find_row(kDecomp, c)) {
        out.append(kDecompPool + (*row)[1], (*row)[2]);
    } else {
        out.push_back(c);
    }
}

char32_t compose_pair(char32_t a, char32_t b) {
    const auto* end = std::end(kCompose);
    const auto* it = std::lower_bound(std::begin(kCompose), end, std::make_pair(a, b),
                                      [](const char32_t (&row)[3], const std::pair<char32_t, char32_t>& key) {
                                          return std::make_pair(row[0], row[1]) < key;
                                      });
    return (it != end && (*it)[0] == a && (*it)[1] == b) ? (*it)[2] : 0;
}
}  // namespace

int combining(char32_t c) {
    const auto* row = find_row(kCombiningClass, c);
    return row ? static_cast<int>((*row)[1]) : 0;
}

U32 nfd(U32View s) {
    U32 out;
    out.reserve(s.size());
    for (char32_t c : s) decompose_into(out, c);
    // Canonical ordering: a stable sort of each run of non-starters.
    size_t i = 0;
    while (i < out.size()) {
        if (combining(out[i]) == 0) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < out.size() && combining(out[j]) != 0) ++j;
        std::stable_sort(out.begin() + static_cast<std::ptrdiff_t>(i), out.begin() + static_cast<std::ptrdiff_t>(j),
                         [](char32_t a, char32_t b) { return combining(a) < combining(b); });
        i = j;
    }
    return out;
}

U32 nfc(U32View s) {
    U32 out = nfd(s);
    if (out.empty()) return out;
    // starter: index of the last starter; last_ccc: -1 when nothing stands
    // between the starter and the current character.
    bool has_starter = combining(out[0]) == 0;
    size_t starter = 0;
    int last_ccc = has_starter ? -1 : 256;
    size_t k = 1;
    for (size_t i = 1; i < out.size(); ++i) {
        const char32_t c = out[i];
        const int ccc = combining(c);
        if (has_starter && (last_ccc == -1 || last_ccc < ccc)) {
            const char32_t first = out[starter];
            char32_t composite = 0;
            if (first >= kLBase && first < kLBase + kLCount && c >= kVBase && c < kVBase + kVCount) {
                if (last_ccc == -1) composite = kSBase + ((first - kLBase) * kVCount + (c - kVBase)) * kTCount;
            } else if (is_hangul(first) && (first - kSBase) % kTCount == 0 && c > kTBase && c < kTBase + kTCount) {
                if (last_ccc == -1) composite = first + (c - kTBase);
            } else {
                composite = compose_pair(first, c);
            }
            if (composite != 0) {
                out[starter] = composite;
                continue;
            }
        }
        if (ccc == 0) {
            has_starter = true;
            starter = k;
            last_ccc = -1;
        } else {
            last_ccc = ccc;
        }
        out[k++] = c;
    }
    out.resize(k);
    return out;
}

char32_t sre_lower(char32_t c) {
    const auto* row = find_row(kSimpleLower, c);
    return row ? (*row)[1] : c;
}

char32_t sre_upper(char32_t c) {
    const auto* row = find_row(kSimpleUpper, c);
    return row ? (*row)[1] : c;
}

bool sre_iscased(char32_t c) { return in_ranges(kSreCased, c); }

std::vector<char32_t> sre_case_fixes(char32_t c) {
    std::vector<char32_t> out;
    if (const auto* row = find_row(kCaseFix, c)) {
        out.push_back((*row)[1]);
        if ((*row)[2] != 0) out.push_back((*row)[2]);
    }
    return out;
}

bool is_printable(char32_t c) { return in_ranges(kPrintable, c); }

bool is_identifier(U32View s) {
    if (s.empty() || !in_ranges(kXidStart, s[0])) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char32_t c) { return in_ranges(kXidContinue, c); });
}

std::string py_str_repr(U32View s) {
    const bool has_single = s.find(U'\'') != U32View::npos;
    const bool has_double = s.find(U'"') != U32View::npos;
    const char32_t quote = (has_single && !has_double) ? U'"' : U'\'';
    U32 out(1, quote);
    char buf[16];
    for (char32_t c : s) {
        if (c == quote || c == U'\\') {
            out += U'\\';
            out += c;
        } else if (c == U'\t') {
            out += U"\\t";
        } else if (c == U'\n') {
            out += U"\\n";
        } else if (c == U'\r') {
            out += U"\\r";
        } else if (c < 0x20 || c == 0x7F) {
            std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(c));
            out += u(buf);
        } else if (c < 0x7F || is_printable(c)) {
            out += c;
        } else if (c <= 0xFF) {
            std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(c));
            out += u(buf);
        } else if (c <= 0xFFFF) {
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
            out += u(buf);
        } else {
            std::snprintf(buf, sizeof buf, "\\U%08x", static_cast<unsigned>(c));
            out += u(buf);
        }
    }
    out += quote;
    return to_utf8(out);
}

}  // namespace tugaphone::detail
