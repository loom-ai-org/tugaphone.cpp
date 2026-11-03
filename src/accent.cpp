#include "tugaphone/accent.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>

#include "accent_internal.hpp"
#include "detail.hpp"
#include "json.hpp"
#include "pyre.hpp"
#include "tugaphone/lattice_core.hpp"
#include "tugaphone/registry.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::accent {

using detail::U32;
using detail::U32View;

namespace {

// ── The default rules' patterns, as hand-written matchers ─────────────────

enum Builtin { kLiteral, kLiteralWordEnd, kCodaL };

struct BuiltinPattern {
    const char* pattern;
    Builtin kind;
    const char32_t* literal;
};

const BuiltinPattern kBuiltins[] = {
    {"v", kLiteral, U"v"},
    {"ti", kLiteral, U"ti"},
    {"di", kLiteral, U"di"},
    {"te\\b", kLiteralWordEnd, U"te"},
    {"de\\b", kLiteralWordEnd, U"de"},
    {"l(?=$|[bcçdfgjkmnpqrstvxz])", kCodaL, U"l"},
    {"ei", kLiteral, U"ei"},
    {"ou", kLiteral, U"ou"},
};

// Python's finditer over one of the default patterns.
std::vector<std::pair<size_t, size_t>> builtin_matches(const BuiltinPattern& b, U32View s) {
    const U32View lit = b.literal;
    std::vector<std::pair<size_t, size_t>> out;
    size_t i = 0;
    while (i + lit.size() <= s.size()) {
        if (s.compare(i, lit.size(), lit) != 0) {
            ++i;
            continue;
        }
        const size_t end = i + lit.size();
        bool ok = true;
        if (b.kind == kLiteralWordEnd) {
            // \b after a word character: the next one is not a word character.
            ok = end == s.size() || !detail::is_word(s[end]);
        } else if (b.kind == kCodaL) {
            // (?=$|[...]): $ also matches before a final newline.
            static const U32View coda = U"bcçdfgjkmnpqrstvxz";
            ok = end == s.size() || (end + 1 == s.size() && s[end] == U'\n') ||
                 coda.find(s[end]) != U32View::npos;
        }
        if (ok) {
            out.emplace_back(i, end);
            i = end;
        } else {
            ++i;
        }
    }
    return out;
}

std::mutex& phonemizer_mutex() {
    static std::mutex m;
    return m;
}
Phonemizer& default_phonemizer() {
    static Phonemizer p;
    return p;
}

std::string run_phonemizer(const Phonemizer& given, const std::string& text, const std::string& lect) {
    if (given) return given(text, lect);
    Phonemizer p;
    {
        std::lock_guard<std::mutex> lock(phonemizer_mutex());
        p = default_phonemizer();
    }
    if (!p) return lattice_core::phonemize(text, lect);
    return p(text, lect);
}

// ── JSON: Python's json.dumps and str() of a json.loads value ─────────────

std::string dumps_str(const std::string& text, bool ensure_ascii) {
    std::string out = "\"";
    char buf[16];
    for (char32_t c : detail::u(text)) {
        switch (c) {
            case U'"': out += "\\\""; continue;
            case U'\\': out += "\\\\"; continue;
            case U'\n': out += "\\n"; continue;
            case U'\r': out += "\\r"; continue;
            case U'\t': out += "\\t"; continue;
            case U'\b': out += "\\b"; continue;
            case U'\f': out += "\\f"; continue;
            default: break;
        }
        if (c < 0x20 || (ensure_ascii && c > 0x7E)) {
            if (c > 0xFFFF) {
                const unsigned v = static_cast<unsigned>(c) - 0x10000;
                std::snprintf(buf, sizeof buf, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
            } else {
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
            }
            out += buf;
        } else {
            out += detail::s8(U32(1, c));
        }
    }
    return out + "\"";
}

std::string py_repr(const json::Value& v);

// str(x) of a value json.loads returned.
std::string py_str(const json::Value& v) {
    switch (v.kind) {
        case json::Value::Null: return "None";
        case json::Value::Bool: return v.boolean ? "True" : "False";
        case json::Value::Int: return v.text == "-0" ? "0" : v.text;
        case json::Value::Float: return float_repr(v.number);
        case json::Value::String: return v.text;
        case json::Value::Array: {
            std::string out = "[";
            for (size_t i = 0; i < v.items.size(); ++i) out += (i ? ", " : "") + py_repr(v.items[i]);
            return out + "]";
        }
        case json::Value::Object: {
            std::string out = "{";
            for (size_t i = 0; i < v.members.size(); ++i) {
                out += (i ? ", " : "") + detail::py_str_repr(detail::u(v.members[i].first)) + ": " +
                       py_repr(v.members[i].second);
            }
            return out + "}";
        }
    }
    return "";
}

std::string py_repr(const json::Value& v) {
    if (v.kind == json::Value::String) return detail::py_str_repr(detail::u(v.text));
    return py_str(v);
}

bool truthy(const json::Value& v) {
    switch (v.kind) {
        case json::Value::Null: return false;
        case json::Value::Bool: return v.boolean;
        case json::Value::Int: return v.text != "0" && v.text != "-0";
        case json::Value::Float: return v.number != 0.0;
        case json::Value::String: return !v.text.empty();
        case json::Value::Array: return !v.items.empty();
        case json::Value::Object: return !v.members.empty();
    }
    return false;
}

const json::Value& as_dict(const json::Value& d) {
    if (d.kind != json::Value::Object) throw Error("AttributeError", "object has no attribute 'get'");
    return d;
}

std::string get_str(const json::Value& d, const char* key, const char* fallback) {
    const json::Value* v = d.find(key);
    return v ? py_str(*v) : fallback;
}

Transform transform_from(const json::Value& d) {
    as_dict(d);
    Transform t;
    t.kind = get_str(d, "kind", "regex");
    t.pattern = get_str(d, "pattern", "");
    t.replacement = get_str(d, "replacement", "");
    t.stage = get_str(d, "stage", "ipa");
    const json::Value* ic = d.find("ignore_case");
    t.ignore_case = ic ? truthy(*ic) : true;
    return t;
}

AccentOverlay overlay_from(const json::Value& d) {
    as_dict(d);
    AccentOverlay o;
    o.name = get_str(d, "name", "");
    const json::Value* items = d.find("transforms");
    if (items == nullptr) return o;
    switch (items->kind) {
        case json::Value::Array:
            for (const auto& t : items->items) o.transforms.push_back(transform_from(t));
            break;
        case json::Value::Object:
            // Iterating a dict yields its keys, which are strings.
            if (!items->members.empty()) throw Error("AttributeError", "'str' object has no attribute 'get'");
            break;
        case json::Value::String:
            if (!items->text.empty()) throw Error("AttributeError", "'str' object has no attribute 'get'");
            break;
        default:
            throw TypeError("object is not iterable");
    }
    return o;
}

// json.dumps of the to_dict() value with the given indent.
struct Writer {
    std::optional<int> indent;
    bool ensure_ascii;

    std::string newline(int level) const {
        if (!indent) return "";
        return "\n" + std::string(static_cast<size_t>(*indent > 0 ? *indent : 0) * static_cast<size_t>(level), ' ');
    }
    std::string item_sep(int level) const { return indent ? "," + newline(level) : ", "; }

    std::string object(const std::vector<std::pair<std::string, std::string>>& members, int level) const {
        if (members.empty()) return "{}";
        std::string out = "{" + newline(level + 1);
        for (size_t i = 0; i < members.size(); ++i) {
            if (i) out += item_sep(level + 1);
            out += dumps_str(members[i].first, ensure_ascii) + ": " + members[i].second;
        }
        return out + newline(level) + "}";
    }
    std::string array(const std::vector<std::string>& items, int level) const {
        if (items.empty()) return "[]";
        std::string out = "[" + newline(level + 1);
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += item_sep(level + 1);
            out += items[i];
        }
        return out + newline(level) + "]";
    }
    std::string transform(const Transform& t, int level) const {
        return object({{"kind", dumps_str(t.kind, ensure_ascii)},
                       {"pattern", dumps_str(t.pattern, ensure_ascii)},
                       {"replacement", dumps_str(t.replacement, ensure_ascii)},
                       {"stage", dumps_str(t.stage, ensure_ascii)},
                       {"ignore_case", t.ignore_case ? "true" : "false"}},
                      level);
    }
    std::string overlay(const AccentOverlay& o) const {
        std::vector<std::string> items;
        for (const auto& t : o.transforms) items.push_back(transform(t, 2));
        return object({{"name", dumps_str(o.name, ensure_ascii)}, {"transforms", array(items, 1)}}, 0);
    }
};

}  // namespace

namespace internal {

size_t levenshtein(U32View a, U32View b) {
    if (a == b) return 0;
    if (a.empty()) return b.size();
    if (b.empty()) return a.size();
    std::vector<size_t> prev(b.size() + 1), cur;
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur.assign(1, i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t sub = prev[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0);
            cur.push_back(std::min({prev[j] + 1, cur[j - 1] + 1, sub}));
        }
        prev.swap(cur);
    }
    return prev.back();
}

U32 restore_case(U32View original, U32View respelled) {
    if (!original.empty() && detail::is_upper(original[0]) && !respelled.empty()) {
        return detail::upper(respelled.substr(0, 1)) + U32(respelled.substr(1));
    }
    return U32(respelled);
}

std::vector<std::pair<size_t, size_t>> word_spans(U32View text) {
    auto letter = [](char32_t c) { return detail::is_word(c) && !detail::is_decimal(c) && c != U'_'; };
    std::vector<std::pair<size_t, size_t>> out;
    size_t i = 0;
    while (i < text.size()) {
        if (!letter(text[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && letter(text[j])) ++j;
        out.emplace_back(i, j);
        i = j;
    }
    return out;
}

}  // namespace internal

void set_default_phonemizer(Phonemizer phonemizer) {
    std::lock_guard<std::mutex> lock(phonemizer_mutex());
    default_phonemizer() = std::move(phonemizer);
}

RespellRule::RespellRule(std::string name, std::string pattern, std::string replacement, std::string note)
    : name_(std::move(name)), pattern_(std::move(pattern)), replacement_(std::move(replacement)),
      note_(std::move(note)), builtin_(-1) {
    for (size_t i = 0; i < std::size(kBuiltins); ++i) {
        if (pattern_ == kBuiltins[i].pattern) builtin_ = static_cast<int>(i);
    }
}

std::vector<std::pair<size_t, size_t>> RespellRule::matches(std::string_view word) const {
    const U32 w = detail::u(word);
    if (builtin_ >= 0) return builtin_matches(kBuiltins[builtin_], w);
    return pyre::Pattern(detail::u(pattern_)).finditer_spans(w);
}

const std::vector<RespellRule>& default_respell_rules() {
    static const std::vector<RespellRule> rules = {
        {"betacism", "v", "b", "[v]→[b] (Northern PT)"},
        {"palatal-ti", "ti", "tchi", "[t]→[tʃ] before [i]"},
        {"palatal-di", "di", "dji", "[d]→[dʒ] before [i]"},
        {"palatal-te", "te\\b", "tchi", "final ⟨te⟩ → [tʃi]"},
        {"palatal-de", "de\\b", "dji", "final ⟨de⟩ → [dʒi]"},
        {"l-vocal", "l(?=$|[bcçdfgjkmnpqrstvxz])", "u", "coda [ɫ]→[w] (Brazilian)"},
        {"mono-ei", "ei", "ê", "[ej]→[e] (some Northern/Brazilian)"},
        {"mono-ou", "ou", "ô", "[ow]→[o]"},
    };
    return rules;
}

std::string respell_word(std::string_view word, const std::string& target_lect, const std::string& base_lect,
                         const std::vector<RespellRule>& rules, const Phonemizer& phonemize) {
    const std::string w(word);
    const U32 target_ipa = detail::u(run_phonemizer(phonemize, w, target_lect));
    const U32 lower = detail::lower(detail::u(word));
    auto base_ipa = [&](const U32& spelling) { return detail::u(run_phonemizer(phonemize, detail::s8(spelling), base_lect)); };

    U32 cur = lower;
    size_t best_d = internal::levenshtein(base_ipa(cur), target_ipa);
    if (best_d == 0) return w;
    bool improved = true;
    while (improved) {
        improved = false;
        std::optional<U32> candidate;
        for (const RespellRule& rule : rules) {
            const U32 replacement = detail::u(rule.replacement());
            for (const auto& [start, end] : rule.matches(detail::s8(cur))) {
                const U32 trial = cur.substr(0, start) + replacement + cur.substr(end);
                if (trial == cur) continue;
                const size_t d = internal::levenshtein(base_ipa(trial), target_ipa);
                if (d < best_d) {
                    best_d = d;
                    candidate = trial;
                }
            }
        }
        if (candidate) {
            cur = *candidate;
            improved = true;
        }
    }
    if (cur == lower) return w;
    return detail::s8(internal::restore_case(detail::u(word), cur));
}

std::string respell(std::string_view text, const std::string& target_lect, const std::string& base_lect,
                    const std::vector<RespellRule>& rules, const Phonemizer& phonemize) {
    const U32 t = detail::u(text);
    std::string out;
    size_t idx = 0;
    for (const auto& [start, end] : internal::word_spans(t)) {
        out += detail::s8(t.substr(idx, start - idx));
        out += respell_word(detail::s8(t.substr(start, end - start)), target_lect, base_lect, rules, phonemize);
        idx = end;
    }
    out += detail::s8(t.substr(idx));
    return out;
}

std::string Transform::apply(std::string_view s) const {
    const int flags = ignore_case ? pyre::IGNORECASE : 0;
    if (kind == "word") {
        const U32 pat = U"\\b" + pyre::escape(detail::u(pattern)) + U"\\b";
        return detail::s8(pyre::Pattern(pat, flags).sub(detail::u(replacement), detail::u(s)));
    }
    if (kind == "regex") {
        return detail::s8(pyre::Pattern(detail::u(pattern), flags).sub(detail::u(replacement), detail::u(s)));
    }
    throw ValueError("unknown transform kind: " + detail::py_str_repr(detail::u(kind)));
}

std::string Transform::to_dict_json() const { return Writer{std::nullopt, false}.transform(*this, 0); }

Transform Transform::from_dict_json(std::string_view text) { return transform_from(json::loads(text)); }

std::string AccentOverlay::apply(std::string_view s, std::string_view stage) const {
    std::string out(s);
    for (const auto& t : transforms) {
        if (t.stage == stage) out = t.apply(out);
    }
    return out;
}

std::string AccentOverlay::to_dict_json() const { return Writer{std::nullopt, false}.overlay(*this); }

AccentOverlay AccentOverlay::from_dict_json(std::string_view text) { return overlay_from(json::loads(text)); }

std::string AccentOverlay::to_json(std::optional<int> indent, bool ensure_ascii) const {
    return Writer{indent, ensure_ascii}.overlay(*this);
}

AccentOverlay AccentOverlay::from_json(std::string_view text) { return overlay_from(json::loads(text)); }

std::string force_accent(std::string_view text, std::string_view lect, std::string_view mode, std::string_view base_lect,
                         const AccentOverlay* overlay, const std::vector<RespellRule>& rules,
                         const Phonemizer& phonemize) {
    const std::string target = registry::resolve_lect(lect);
    const std::string base = registry::resolve_lect(base_lect);
    if (mode == "ipa") {
        std::string out = run_phonemizer(phonemize, std::string(text), target);
        if (overlay != nullptr) out = overlay->apply(out, "ipa");
        return out;
    }
    if (mode == "respell") {
        std::string out = respell(text, target, base, rules, phonemize);
        if (overlay != nullptr) out = overlay->apply(out, "text");
        return out;
    }
    throw ValueError("mode must be 'ipa' or 'respell', got " + detail::py_str_repr(detail::u(mode)));
}

}  // namespace tugaphone::accent
