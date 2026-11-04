#include <algorithm>
#include <cstdio>
#include <iterator>

#include "detail.hpp"

namespace tugaphone::detail {

namespace {
#include "unicode_tables.inc"

template <size_t N>
bool in_ranges(const char32_t (&table)[N][2], char32_t c) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < table[mid][0]) {
            hi = mid;
        } else if (c > table[mid][1]) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

template <size_t N>
const CaseEntry* find_case(const CaseEntry (&table)[N], char32_t c) {
    const auto* end = std::end(table);
    const auto* it = std::lower_bound(std::begin(table), end, c,
                                      [](const CaseEntry& e, char32_t v) { return e.cp < v; });
    return (it != end && it->cp == c) ? it : nullptr;
}

constexpr char32_t kCapitalSigma = 0x03A3;

// Python's handle_capital_sigma: Σ lowercases to final ς when it follows a
// cased letter (skipping case-ignorables) and no cased letter follows it.
char32_t lower_sigma(U32View text, size_t i) {
    size_t j = i;
    char32_t c = 0;
    bool found = false;
    while (j > 0) {
        c = text[--j];
        if (!in_ranges(kCaseIgnorable, c)) {
            found = true;
            break;
        }
    }
    bool final_sigma = found && in_ranges(kCased, c);
    if (final_sigma && i + 1 < text.size()) {
        size_t k = i + 1;
        for (; k < text.size(); ++k) {
            c = text[k];
            if (!in_ranges(kCaseIgnorable, c)) break;
        }
        final_sigma = k == text.size() || !in_ranges(kCased, c);
    }
    return final_sigma ? 0x03C2 : 0x03C3;
}

void append_lower(U32& out, U32View text, size_t i) {
    const char32_t c = text[i];
    if (c == kCapitalSigma) {
        out.push_back(lower_sigma(text, i));
    } else if (const CaseEntry* e = find_case(kLower, c)) {
        out.append(e->to, e->len);
    } else {
        out.push_back(c);
    }
}

bool in_set(U32View chars, char32_t c) { return chars.find(c) != U32View::npos; }
}  // namespace

U32 from_utf8(std::string_view text) {
    U32 out;
    out.reserve(text.size());
    const size_t n = text.size();
    auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    size_t i = 0;
    while (i < n) {
        const unsigned char b = byte(i);
        if (b < 0x80) {
            out.push_back(b);
            ++i;
            continue;
        }
        size_t len;
        char32_t cp;
        unsigned char lo = 0x80, hi = 0xBF;  // the range of the second byte
        if (b >= 0xC2 && b <= 0xDF) {
            len = 2;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            len = 3;
            cp = b & 0x0F;
            if (b == 0xE0) lo = 0xA0;
            if (b == 0xED) hi = 0x9F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            len = 4;
            cp = b & 0x07;
            if (b == 0xF0) lo = 0x90;
            if (b == 0xF4) hi = 0x8F;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        size_t k = 1;
        for (; k < len && i + k < n; ++k) {
            const unsigned char c = byte(i + k);
            const unsigned char min = k == 1 ? lo : 0x80, max = k == 1 ? hi : 0xBF;
            if (c < min || c > max) break;
            cp = (cp << 6) | (c & 0x3F);
        }
        out.push_back(k == len ? cp : 0xFFFD);
        i += k;
    }
    return out;
}

std::optional<std::string> utf8_decode_error(std::string_view text) {
    const size_t n = text.size();
    auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    auto error = [&](size_t start, size_t end, const char* reason) {
        char buf[128];
        if (end - start == 1)
            std::snprintf(buf, sizeof buf, "'utf-8' codec can't decode byte 0x%02x in position %zu: %s", byte(start),
                          start, reason);
        else
            std::snprintf(buf, sizeof buf, "'utf-8' codec can't decode bytes in position %zu-%zu: %s", start, end - 1,
                          reason);
        return std::optional<std::string>(buf);
    };
    size_t i = 0;
    while (i < n) {
        const unsigned char b = byte(i);
        if (b < 0x80) {
            ++i;
            continue;
        }
        size_t len;
        unsigned char lo = 0x80, hi = 0xBF;  // the range of the second byte
        if (b >= 0xC2 && b <= 0xDF) {
            len = 2;
        } else if (b >= 0xE0 && b <= 0xEF) {
            len = 3;
            if (b == 0xE0) lo = 0xA0;
            if (b == 0xED) hi = 0x9F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            len = 4;
            if (b == 0xF0) lo = 0x90;
            if (b == 0xF4) hi = 0x8F;
        } else {
            return error(i, i + 1, "invalid start byte");
        }
        for (size_t k = 1; k < len; ++k) {
            if (i + k == n) return error(i, n, "unexpected end of data");
            const unsigned char c = byte(i + k);
            const unsigned char min = k == 1 ? lo : 0x80, max = k == 1 ? hi : 0xBF;
            if (c < min || c > max) return error(i, i + k, "invalid continuation byte");
        }
        i += len;
    }
    return std::nullopt;
}

std::string to_utf8(U32View text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

bool is_space(char32_t c) { return in_ranges(kSpace, c); }
bool is_word(char32_t c) { return in_ranges(kWord, c); }
bool is_alnum(char32_t c) { return c != '_' && in_ranges(kWord, c); }
bool is_alpha(char32_t c) { return in_ranges(kIsAlpha, c); }
bool is_upper(char32_t c) { return in_ranges(kIsUpper, c); }
bool is_digit(char32_t c) { return in_ranges(kIsDigit, c); }

int decimal_value(char32_t c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    const auto* end = std::end(kDecimal);
    const auto* it = std::lower_bound(std::begin(kDecimal), end, c,
                                      [](const char32_t (&e)[2], char32_t v) { return e[0] < v; });
    return (it != end && (*it)[0] == c) ? static_cast<int>((*it)[1]) : -1;
}

bool is_decimal(char32_t c) { return decimal_value(c) >= 0; }

bool all_alpha(U32View s) { return !s.empty() && std::all_of(s.begin(), s.end(), is_alpha); }
bool all_digit(U32View s) { return !s.empty() && std::all_of(s.begin(), s.end(), is_digit); }

U32 lower(U32View s) {
    U32 out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) append_lower(out, s, i);
    return out;
}

U32 upper(U32View s) {
    U32 out;
    out.reserve(s.size());
    for (char32_t c : s) {
        if (const CaseEntry* e = find_case(kUpper, c)) {
            out.append(e->to, e->len);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Python's str.capitalize: the first code point in title case, the rest lower.
U32 capitalize(U32View s) {
    U32 out;
    if (s.empty()) return out;
    if (const CaseEntry* e = find_case(kTitle, s[0])) {
        out.append(e->to, e->len);
    } else {
        out.push_back(s[0]);
    }
    for (size_t i = 1; i < s.size(); ++i) append_lower(out, s, i);
    return out;
}

std::vector<U32> split_ws(U32View s) {
    std::vector<U32> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) ++i;
        if (i == s.size()) break;
        size_t j = i;
        while (j < s.size() && !is_space(s[j])) ++j;
        out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

U32 strip_ws(U32View s) {
    size_t b = 0, e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return U32(s.substr(b, e - b));
}

U32 lstrip(U32View s, U32View chars) {
    size_t b = 0;
    while (b < s.size() && in_set(chars, s[b])) ++b;
    return U32(s.substr(b));
}

U32 rstrip(U32View s, U32View chars) {
    size_t e = s.size();
    while (e > 0 && in_set(chars, s[e - 1])) --e;
    return U32(s.substr(0, e));
}

U32 strip(U32View s, U32View chars) { return lstrip(rstrip(s, chars), chars); }

U32 join(const std::vector<U32>& parts, U32View sep) {
    U32 out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(sep);
        out.append(parts[i]);
    }
    return out;
}

U32 replace(U32View s, U32View from, U32View to, long count) {
    U32 out;
    if (from.empty()) {
        // Python inserts `to` before every code point and at the end.
        long done = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (count < 0 || done < count) {
                out.append(to);
                ++done;
            }
            if (i < s.size()) out.push_back(s[i]);
        }
        return out;
    }
    size_t i = 0;
    long done = 0;
    while (i < s.size()) {
        if ((count < 0 || done < count) && s.compare(i, from.size(), from) == 0) {
            out.append(to);
            i += from.size();
            ++done;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

bool contains(U32View s, U32View part) { return s.find(part) != U32View::npos; }
bool contains(U32View s, char32_t c) { return s.find(c) != U32View::npos; }
bool starts_with(U32View s, U32View prefix) { return s.substr(0, prefix.size()) == prefix; }
bool ends_with(U32View s, U32View suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace tugaphone::detail
