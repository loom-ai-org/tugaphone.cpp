#include "nfd.hpp"

#include <algorithm>
#include <iterator>

namespace tugaphone::detail {

namespace {

struct NfdEntry {
    char32_t cp;
    char32_t to[4];
    int len;
};
struct CccRange {
    char32_t lo, hi;
    int ccc;
};

#include "unicode_nfd.inc"

constexpr char32_t kSBase = 0xAC00, kLBase = 0x1100, kVBase = 0x1161, kTBase = 0x11A7;
constexpr char32_t kLCount = 19, kVCount = 21, kTCount = 28;
constexpr char32_t kNCount = kVCount * kTCount, kSCount = kLCount * kNCount;

void decompose(char32_t c, U32& out) {
    if (c >= kSBase && c < kSBase + kSCount) {
        const char32_t s = c - kSBase;
        out.push_back(kLBase + s / kNCount);
        out.push_back(kVBase + (s % kNCount) / kTCount);
        if (s % kTCount) out.push_back(kTBase + s % kTCount);
        return;
    }
    const auto it = std::lower_bound(std::begin(kNfd), std::end(kNfd), c,
                                     [](const NfdEntry& e, char32_t v) { return e.cp < v; });
    if (it != std::end(kNfd) && it->cp == c) {
        out.append(it->to, static_cast<size_t>(it->len));
    } else {
        out.push_back(c);
    }
}

}  // namespace

int combining(char32_t c) {
    const auto it = std::upper_bound(std::begin(kCcc), std::end(kCcc), c,
                                     [](char32_t v, const CccRange& r) { return v < r.lo; });
    if (it == std::begin(kCcc)) return 0;
    const CccRange& r = *std::prev(it);
    return c <= r.hi ? r.ccc : 0;
}

U32 nfd(U32View text) {
    U32 out;
    for (char32_t c : text) decompose(c, out);
    // Canonical ordering: a stable sort by combining class of each run of
    // non-starters.
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

}  // namespace tugaphone::detail
