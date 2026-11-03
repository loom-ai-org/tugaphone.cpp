#include "tugaphone/tokenizer.hpp"

#include <map>
#include <mutex>

#include <bifonia/bifonia.hpp>
#include <orthography2ipa/orthography2ipa.hpp>
#include <orthography2ipa/stress.hpp>
#include <orthography2ipa/vowels.hpp>
#include <silabificador/silabificador.hpp>

#include "detail.hpp"
#include "nfd.hpp"
#include "o2i_registry.hpp"
#include "tugaphone/number_utils.hpp"

namespace tugaphone::tokenizer {

namespace o2i = orthography2ipa;
using detail::s8;
using detail::u;
using detail::U32;
using detail::U32View;

namespace {

// Python's `needle in haystack` for two str: "" is in every string.
bool in_str(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

bool in_set(const std::set<std::string>& set, const std::string& key) { return set.count(key) != 0; }

template <class T>
T& py_index(std::vector<T>& items, long index) {
    const long n = static_cast<long>(items.size());
    const long at = index < 0 ? index + n : index;
    if (at < 0 || at >= n) throw IndexError("list index out of range");
    return items[static_cast<size_t>(at)];
}

template <class T>
const T& py_index(const std::vector<T>& items, long index) {
    return py_index(const_cast<std::vector<T>&>(items), index);
}

long py_len(std::string_view text) { return static_cast<long>(u(text).size()); }

// text[start:stop] over code points, with Python's clamping.
std::string py_slice(std::string_view text, long start, std::optional<long> stop = std::nullopt) {
    const U32 s = u(text);
    const long n = static_cast<long>(s.size());
    auto clamp = [n](long i) { return i < 0 ? std::max(0L, i + n) : std::min(i, n); };
    const long b = clamp(start), e = clamp(stop.value_or(n));
    if (b >= e) return "";
    return s8(U32View(s).substr(static_cast<size_t>(b), static_cast<size_t>(e - b)));
}

// One code point of `text` as UTF-8, by Python index.
std::string py_char(std::string_view text, long index) {
    const U32 s = u(text);
    const long n = static_cast<long>(s.size());
    const long at = index < 0 ? index + n : index;
    if (at < 0 || at >= n) throw IndexError("string index out of range");
    return s8(U32(1, s[static_cast<size_t>(at)]));
}

std::vector<std::string> code_points(std::string_view text) {
    std::vector<std::string> out;
    for (char32_t c : u(text)) out.push_back(s8(U32(1, c)));
    return out;
}

const std::string* map_get(const dialects::StrMap& map, std::string_view key) { return map.find(key); }

// The shared orthography2ipa grapheme tokenizer of a dialect inventory
// (_phonetok): each grapheme of the inventory maps to itself.
struct Phonetok {
    o2i::LanguageSpec spec;
    std::unique_ptr<o2i::PhonetokTokenizer> tokenizer;
};

const o2i::PhonetokTokenizer& phonetok(const std::string& dialect_code, const std::vector<std::string>& inventory) {
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Phonetok>> cache;
    std::string key = dialect_code;
    for (const auto& g : inventory) key += '\x1f' + g;
    const std::lock_guard<std::mutex> guard(mutex);
    auto& slot = cache[key];
    if (!slot) {
        slot = std::make_unique<Phonetok>();
        slot->spec.code = dialect_code;
        slot->spec.name = "pt";
        slot->spec.family = "portuguese";
        slot->spec.script = "Latn";
        for (const auto& g : inventory) slot->spec.graphemes[g] = {g};
        slot->tokenizer = std::make_unique<o2i::PhonetokTokenizer>(slot->spec);
    }
    return *slot->tokenizer;
}

// _spec_stress_rules(dialect_code): the spec whose stress block applies, or
// null when the code has no spec or the spec has no stress block.
const o2i::LanguageSpec* spec_stress_rules(const std::string& dialect_code) {
    static std::map<std::string, const o2i::LanguageSpec*> cache;
    const std::lock_guard<std::mutex> guard(detail::o2i_registry_mutex());
    const auto it = cache.find(dialect_code);
    if (it != cache.end()) return it->second;
    const o2i::LanguageSpec* rules = nullptr;
    try {
        const o2i::LanguageSpec& spec = o2i::get(dialect_code);
        if (spec.stress_defined) rules = &spec;
    } catch (const std::exception&) {
        rules = nullptr;
    }
    cache[dialect_code] = rules;
    return rules;
}

bool is_cliticless(const o2i::LanguageSpec& rules, const std::string& word) {
    for (const auto& w : rules.cliticless_words) {
        if (w == word) return true;
    }
    return false;
}

const DialectPtr& default_dialect() {
    static const DialectPtr dialect = std::make_shared<dialects::EuropeanPortuguese>();
    return dialect;
}

DialectPtr or_default(DialectPtr dialect) { return dialect ? std::move(dialect) : default_dialect(); }

// The inflected forms of PRONOUNCED_QU_GU_U (_inflections).
std::set<std::string> inflections(const std::string& entry) {
    const U32 e = u(entry);
    auto ends = [&](std::string_view suffix) { return detail::ends_with(e, u(suffix)); };
    if (ends("mente")) return {entry};
    if (ends("o")) {
        const std::string stem = s8(U32View(e).substr(0, e.size() - 1));
        return {stem + "o", stem + "a", stem + "os", stem + "as"};
    }
    if (ends("im")) return {entry, s8(U32View(e).substr(0, e.size() - 1)) + "ns"};
    if (ends("a") || ends("e")) return {entry, entry + "s"};
    return {entry};
}

const std::vector<std::string>& pronounced_verb_stems() {
    static const std::vector<std::string> stems = [] {
        std::vector<std::string> out;
        for (const auto& entry : PRONOUNCED_QU_GU_U()) {
            const U32 e = u(entry);
            const bool verb = detail::ends_with(e, U"ar") || detail::ends_with(e, U"er") || detail::ends_with(e, U"ir");
            if (verb && !in_str("aeiou", py_char(entry, -3))) out.push_back(s8(U32View(e).substr(0, e.size() - 2)));
        }
        return out;
    }();
    return stems;
}

const std::set<std::string>& pronounced_forms() {
    static const std::set<std::string> forms = [] {
        std::set<std::string> out;
        for (const auto& entry : PRONOUNCED_QU_GU_U()) {
            const auto f = inflections(entry);
            out.insert(f.begin(), f.end());
        }
        return out;
    }();
    return forms;
}

// d.VOWEL_CHARS | ACUTE | GRAVE | CIRCUM | TILDE | NORMALIZED_VOWELS.keys().
bool in_all_vowels(const DialectInventory& d, const std::string& s) {
    return in_set(d.VOWEL_CHARS, s) || in_set(d.ACUTE_VOWEL_CHARS, s) || in_set(d.GRAVE_VOWEL_CHARS, s) ||
           in_set(d.CIRCUM_VOWEL_CHARS, s) || in_set(d.TILDE_VOWEL_CHARS, s) || d.NORMALIZED_VOWELS.contains(s);
}

// set("bcdfghjklmnpqrstvwxyz") - all_vowels.
bool in_consonants(const DialectInventory& d, const std::string& s) {
    return py_len(s) == 1 && in_str("bcdfghjklmnpqrstvwxyz", s) && !in_all_vowels(d, s);
}

bool in_vowel_letters(const DialectInventory& d, const std::string& s) {
    return in_set(d.ALL_VOWEL_CHARS, s) || in_set(d.TILDE_VOWEL_CHARS, s);
}

// A Python dict under construction: assigning an existing key replaces its
// value in place.
class FeatureDict {
public:
    explicit FeatureDict(Features base) : items_(std::move(base)) {
        for (size_t i = 0; i < items_.size(); ++i) index_[items_[i].first] = i;
    }
    void set(std::string key, FeatureValue value) {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            items_[it->second].second = std::move(value);
            return;
        }
        index_.emplace(key, items_.size());
        items_.emplace_back(std::move(key), std::move(value));
    }
    Features take() { return std::move(items_); }

private:
    Features items_;
    std::map<std::string, size_t> index_;
};

FeatureValue fv(const std::optional<std::string>& v) {
    if (v) return *v;
    return std::monostate{};
}

void quote(std::string& out, const std::string& text) {
    out += '"';
    for (unsigned char c : text) {
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
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

}  // namespace

// ── module functions ─────────────────────────────────────────────────────────

int detect_stress_position(std::string_view word, const std::vector<std::string>& syllables,
                           const DialectInventory& dialect) {
    const long n = static_cast<long>(syllables.size());
    if (n == 1) return 0;
    if (const o2i::LanguageSpec* rules = spec_stress_rules(dialect.dialect_code)) {
        return o2i::stress::detect_stress(std::string(word), *rules, &syllables);
    }
    for (long idx = 0; idx < n; ++idx) {
        for (const auto& marker : dialect.PRIMARY_STRESS_MARKERS) {
            if (in_str(syllables[static_cast<size_t>(idx)], marker)) return static_cast<int>(idx);
        }
    }
    const U32 w = u(word);
    for (const auto& ending : dialect.OXYTONE_ENDINGS) {
        if (detail::ends_with(w, u(ending))) return static_cast<int>(n - 1);
    }
    return n >= 2 ? static_cast<int>(n - 2) : 0;
}

const std::set<std::string>& PRONOUNCED_QU_GU_U() {
    static const std::set<std::string> words = {
        "aguentar",  "anguiforme", "antiguidade", "arguir",     "cinquenta", "equestre",
        "equino",    "frequentar", "frequente",   "frequentemente", "lingueta", "linguiça",
        "linguista", "linguístico", "pinguim",    "tranquilo",  "ubiquidade",
    };
    return words;
}

bool pronounces_qu_gu_u(std::string_view word) {
    const U32 w = detail::lower(u(word));
    const std::string ws = s8(w);
    if (in_set(pronounced_forms(), ws)) return true;
    for (const auto& stem : pronounced_verb_stems()) {
        if (detail::starts_with(w, u(stem))) return true;
    }
    return false;
}

bool is_grapheme_silent(std::string_view grapheme, std::string_view context_before,
                        std::string_view context_after, std::string_view word, const DialectInventory& dialect) {
    const std::string g = s8(detail::lower(u(grapheme)));
    const std::string before = s8(detail::lower(u(context_before)));
    const std::string after = s8(detail::lower(u(context_after)));
    if (g == "h") return true;
    const std::string last = py_slice(before, -1);
    if (g == "u" && (last == "q" || last == "g") && !after.empty() && in_str("eiéêí", py_char(after, 0))) {
        return !pronounces_qu_gu_u(word);
    }
    if (g == "p" && before == "m" && !after.empty() && in_str("cç", py_char(after, 0))) {
        for (const auto& [cluster, words] : dialect.ARCHAIC_MUTE_P) {
            if (in_str(word, cluster) && words.count(std::string(word))) return true;
        }
    }
    return false;
}

std::string features_json(const Features& features) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : features) {
        if (!first) out += ',';
        first = false;
        quote(out, key);
        out += ':';
        if (std::holds_alternative<std::monostate>(value)) {
            out += "null";
        } else if (const bool* b = std::get_if<bool>(&value)) {
            out += *b ? "true" : "false";
        } else if (const long* i = std::get_if<long>(&value)) {
            out += std::to_string(*i);
        } else if (const std::string* s = std::get_if<std::string>(&value)) {
            quote(out, *s);
        } else {
            out += '[';
            const auto& items = std::get<std::vector<std::string>>(value);
            for (size_t k = 0; k < items.size(); ++k) {
                if (k) out += ',';
                quote(out, items[k]);
            }
            out += ']';
        }
    }
    return out + "}";
}

// ── CharToken ────────────────────────────────────────────────────────────────

CharToken::CharToken(std::string surface, long char_idx, GraphemeToken* parent_grapheme)
    : surface_(std::move(surface)), char_idx_(char_idx), parent_grapheme_(parent_grapheme) {
    if (py_len(surface_) != 1) throw ValueError("CharToken must contain exactly one character, got: " + surface_);
    if (!parent_grapheme_) throw AttributeError("'NoneType' object has no attribute 'idx_in_word'");
    idx_in_word_ = parent_grapheme_->idx_in_word() + char_idx_;
    idx_in_sentence_ = parent_grapheme_->idx_in_sentence() + char_idx_;
}

const DialectInventory& CharToken::dialect() const { return parent_grapheme_->dialect(); }

const std::string& CharToken::normalized() const {
    if (!normalized_) {
        const std::string s = s8(detail::strip_ws(detail::lower(u(surface_))));
        const std::string* mapped = map_get(dialect().NORMALIZED_VOWELS, s);
        normalized_ = mapped ? *mapped : s;
    }
    return *normalized_;
}

WordToken* CharToken::parent_word() const { return parent_grapheme_ ? parent_grapheme_->parent_word() : nullptr; }

Sentence* CharToken::parent_sentence() const {
    WordToken* word = parent_word();
    return word ? word->parent_sentence() : nullptr;
}

const CharToken* CharToken::prev_char() const {
    if (!parent_grapheme_) return nullptr;
    if (char_idx_ == 0) {
        const GraphemeToken* prev = parent_grapheme_->prev_grapheme();
        return prev ? &prev->last_char() : nullptr;
    }
    return py_index(parent_grapheme_->characters(), char_idx_ - 1).get();
}

const CharToken* CharToken::next_char() const {
    if (char_idx_ == -1 || !parent_grapheme_) return nullptr;
    if (char_idx_ >= parent_grapheme_->n_chars() - 1) {
        const GraphemeToken* next = parent_grapheme_->next_grapheme();
        return next ? &next->first_char() : nullptr;
    }
    return py_index(parent_grapheme_->characters(), char_idx_ + 1).get();
}

std::string CharToken::prefix() const {
    std::string out = parent_grapheme_->prefix();
    if (char_idx_ == 0) return out;
    for (const auto& c : parent_grapheme_->characters()) {
        if (c->char_idx_ < char_idx_) out += c->normalized();
    }
    return out;
}

std::string CharToken::suffix() const {
    std::string out;
    for (const auto& c : parent_grapheme_->characters()) {
        if (c->char_idx_ > char_idx_) out += c->normalized();
    }
    return out + parent_grapheme_->suffix();
}

bool CharToken::is_punct() const { return in_set(dialect().PUNCT_CHARS, surface_); }

bool CharToken::is_vowel() const { return o2i::vowels::is_orthographic_vowel(normalized()); }

bool CharToken::is_semivowel() const { return in_set(dialect().SEMIVOWEL_CHARS, normalized()); }

bool CharToken::is_consonant() const { return !is_vowel() && !is_punct(); }

bool CharToken::is_nasal_vowel() const {
    if (!is_vowel()) return false;
    if (in_set(dialect().TILDE_VOWEL_CHARS, normalized())) return true;
    const CharToken* next = next_char();
    if (next && in_str("mn", next->normalized())) {
        const CharToken* next_next = next->next_char();
        if (!next_next || next_next->is_consonant()) return true;
    }
    return false;
}

bool CharToken::is_foreign() const { return in_set(dialect().FOREIGN_CHARS, normalized()); }

bool CharToken::has_diacritics() const {
    const auto& d = dialect();
    const std::string& s = normalized();
    return in_set(d.ACUTE_VOWEL_CHARS, s) || in_set(d.GRAVE_VOWEL_CHARS, s) || in_set(d.CIRCUM_VOWEL_CHARS, s) ||
           in_set(d.TILDE_VOWEL_CHARS, s) || in_set(d.TREMA_VOWEL_CHARS, s);
}

bool CharToken::is_silent() const {
    if (!is_silent_) {
        const WordToken* word = parent_word();
        is_silent_ = is_grapheme_silent(normalized(), prefix(), suffix(), word ? word->normalized() : "", dialect());
    }
    return *is_silent_;
}

bool CharToken::is_open_vowel() const {
    return in_set(dialect().ACUTE_VOWEL_CHARS, normalized()) || normalized() == "a";
}

bool CharToken::is_closed_vowel() const {
    const std::string& s = normalized();
    return s == "i" || s == "u" || s == "ê" || s == "ô";
}

std::optional<std::string> CharToken::vowel_phone() const {
    if (!is_vowel()) return std::nullopt;
    for (char32_t c : detail::nfd(u(ipa()))) {
        const std::string phone = s8(U32(1, c));
        if (dialect().VOWEL_HEIGHT.contains(phone)) return phone;
    }
    return std::nullopt;
}

std::optional<std::string> CharToken::vowel_height() const {
    const auto phone = vowel_phone();
    if (!phone) return std::nullopt;
    const std::string* v = map_get(dialect().VOWEL_HEIGHT, *phone);
    return v ? std::optional<std::string>(*v) : std::nullopt;
}

std::optional<std::string> CharToken::vowel_backness() const {
    const auto phone = vowel_phone();
    if (!phone) return std::nullopt;
    const std::string* v = map_get(dialect().VOWEL_BACKNESS, *phone);
    return v ? std::optional<std::string>(*v) : std::nullopt;
}

std::optional<std::string> CharToken::vowel_roundedness() const {
    const auto phone = vowel_phone();
    if (!phone) return std::nullopt;
    return in_set(dialect().ROUNDED_VOWEL_PHONES, *phone) ? "rounded" : "unrounded";
}

bool CharToken::is_front_vowel() const { return vowel_backness() == "front"; }
bool CharToken::is_back_vowel() const { return vowel_backness() == "back"; }
bool CharToken::is_rounded_vowel() const { return vowel_roundedness() == "rounded"; }

std::optional<std::string> CharToken::manner_of_articulation() const {
    if (!is_consonant()) return std::nullopt;
    const std::string* v = map_get(dialect().MANNER_OF_ARTICULATION, normalized());
    return v ? std::optional<std::string>(*v) : std::nullopt;
}

std::optional<std::string> CharToken::place_of_articulation() const {
    if (!is_consonant()) return std::nullopt;
    const std::string* v = map_get(dialect().PLACE_OF_ARTICULATION, normalized());
    return v ? std::optional<std::string>(*v) : std::nullopt;
}

std::optional<std::string> CharToken::voicing() const {
    if (is_punct()) return std::nullopt;
    if (is_vowel()) return "voiced";
    const std::string& s = normalized();
    if (s == "s" && is_intervocalic()) return "voiced";
    if (in_set(dialect().VOICED_CONSONANT_CHARS, s)) return "voiced";
    if (in_set(dialect().VOICELESS_CONSONANT_CHARS, s)) return "voiceless";
    return std::nullopt;
}

bool CharToken::is_plosive() const { return manner_of_articulation() == "plosive"; }
bool CharToken::is_fricative() const { return manner_of_articulation() == "fricative"; }

bool CharToken::is_nasal_consonant() const {
    return is_consonant() && in_set(dialect().NASAL_CONSONANT_CHARS, normalized());
}
bool CharToken::is_liquid() const { return is_consonant() && in_set(dialect().LIQUID_CHARS, normalized()); }
bool CharToken::is_sibilant() const { return is_consonant() && in_set(dialect().SIBILANT_CHARS, normalized()); }
bool CharToken::is_rhotic() const { return is_consonant() && in_set(dialect().RHOTIC_CHARS, normalized()); }

bool CharToken::is_sonorant() const {
    if (is_vowel()) return true;
    const auto manner = manner_of_articulation();
    return manner == "nasal" || manner == "lateral" || manner == "rhotic";
}

bool CharToken::is_obstruent() const {
    const auto manner = manner_of_articulation();
    return manner == "plosive" || manner == "fricative";
}

std::optional<std::string> CharToken::parent_syllable() const {
    const GraphemeToken* g = parent_grapheme_;
    if (!g || !g->parent_word() || g->syllable_idx() < 0) return std::nullopt;
    const auto& syllables = g->parent_word()->normalized_syllables();
    if (g->syllable_idx() >= static_cast<long>(syllables.size())) return std::nullopt;
    return syllables[static_cast<size_t>(g->syllable_idx())];
}

long CharToken::idx_in_syllable() const {
    const GraphemeToken* g = parent_grapheme_;
    if (!g || !g->parent_word() || !parent_syllable()) return -1;
    const auto& syllables = g->parent_word()->normalized_syllables();
    long preceding = 0;
    const long stop = std::min(g->syllable_idx(), static_cast<long>(syllables.size()));
    for (long i = 0; i < stop; ++i) preceding += py_len(syllables[static_cast<size_t>(i)]);
    return idx_in_word_ - preceding;
}

bool CharToken::is_nucleus() const {
    if (!is_vowel()) return false;
    const GraphemeToken* g = parent_grapheme_;
    if (g && g->is_triphthong()) return 0 < char_idx_ && char_idx_ < g->n_chars() - 1;
    if (g && g->is_diphthong()) {
        if (g->is_rising_diphthong()) return char_idx_ == g->n_chars() - 1;
        return char_idx_ == 0;
    }
    return true;
}

bool CharToken::is_onset() const {
    if (!is_consonant() || is_silent()) return false;
    const auto syllable = parent_syllable();
    const long idx = idx_in_syllable();
    if (!syllable || idx < 0) return false;
    for (const auto& ch : code_points(py_slice(*syllable, 0, idx))) {
        if (in_vowel_letters(dialect(), ch)) return false;
    }
    return true;
}

bool CharToken::is_coda() const {
    if (!is_consonant() || is_silent()) return false;
    const auto syllable = parent_syllable();
    const long idx = idx_in_syllable();
    if (!syllable || idx < 0) return false;
    for (const auto& ch : code_points(py_slice(*syllable, 0, idx))) {
        if (in_vowel_letters(dialect(), ch)) return true;
    }
    return false;
}

bool CharToken::is_first_word_letter() const { return idx_in_word_ == 0; }

bool CharToken::is_last_word_letter() const {
    const WordToken* word = parent_word();
    if (!word) return false;
    return idx_in_word_ == py_len(word->normalized()) - 1;
}

const std::string& CharToken::prev_letter() const {
    if (!prev_letter_) {
        if (const CharToken* prev = prev_char()) {
            prev_letter_ = prev->normalized();
        } else {
            const std::string p = prefix();
            prev_letter_ = p.empty() ? "" : py_char(p, -1);
        }
    }
    return *prev_letter_;
}

const std::string& CharToken::next_letter() const {
    if (!next_letter_) {
        if (const CharToken* next = next_char()) {
            next_letter_ = next->normalized();
        } else {
            const std::string s = suffix();
            next_letter_ = s.empty() ? "" : py_char(s, 0);
        }
    }
    return *next_letter_;
}

bool CharToken::is_intervocalic() const {
    return in_all_vowels(dialect(), prev_letter()) && in_all_vowels(dialect(), next_letter());
}

bool CharToken::is_between_consonant_vowel() const {
    return in_consonants(dialect(), prev_letter()) && in_all_vowels(dialect(), next_letter());
}

bool CharToken::is_between_vowel_consonant() const {
    return in_all_vowels(dialect(), prev_letter()) && in_consonants(dialect(), next_letter());
}

bool CharToken::has_primary_stress() const {
    if (!has_primary_stress_) {
        if (in_set(dialect().PRIMARY_STRESS_MARKERS, normalized())) {
            has_primary_stress_ = true;
        } else {
            has_primary_stress_ = parent_grapheme_ ? parent_grapheme_->has_primary_stress() : false;
        }
    }
    return *has_primary_stress_;
}

bool CharToken::has_secondary_stress() const {
    if (!has_secondary_stress_) {
        bool result;
        if (in_set(dialect().SECONDARY_STRESS_MARKERS, normalized())) {
            result = true;
        } else {
            const CharToken* prev = is_vowel() ? prev_char() : nullptr;
            if (prev && prev->normalized() == "h" && prev->parent_grapheme_->surface() == "h") {
                result = true;
            } else {
                result = parent_grapheme_ ? parent_grapheme_->has_secondary_stress() : false;
            }
        }
        has_secondary_stress_ = result;
    }
    return *has_secondary_stress_;
}

std::string CharToken::ipa_for_vowel() const {
    const auto& d = dialect();
    const std::string& s = normalized();
    if (s == "u" && (prev_letter() == "q" || prev_letter() == "g") && in_vowel_letters(d, next_letter())) return "w";

    const std::string* base = map_get(d.DEFAULT_CHAR2PHONEMES, s);
    if (!base) return s;
    const std::string base_ipa = *base;
    const WordToken* parent = parent_word();
    const std::string word = parent ? parent->normalized() : "";

    if (word == "a") return "ɐ";
    if (word == "e") return "i";
    if (word == "é") return "ɛ";
    if (word == "o") return "u";

    static const std::set<std::string> atonic = {
        "a",  "o",   "as",  "os",  "de",  "em",  "por",  "da",   "do",   "das", "dos", "me",  "te",
        "se", "le",  "lo",  "la",  "les", "los", "las",  "lhe",  "lho",  "lha", "lhes", "lhos", "lhas",
        "na", "no",  "nas", "nos", "pra",
    };
    const std::string& dc = d.dialect_code;
    auto starts = [&](std::string_view p) { return dc.compare(0, p.size(), p) == 0; };
    if (atonic.count(word)) {
        if (starts("pt-BR")) {
            if (s == "a") return "a";
            if (s == "e") return "e";
            if (s == "o") return "o";
        } else {
            if (s == "a") return "ɐ";
            if (s == "e") return "ɨ";
            if (s == "o") return "u";
        }
    }

    if (s == "a") {
        if (starts("pt-TL")) return has_primary_stress() || has_secondary_stress() ? "a" : "ə";
        if (starts("pt-AO") || starts("pt-BR")) return "a";
        return has_primary_stress() || has_secondary_stress() ? "a" : "ɐ";
    }
    if (s == "e") {
        if (has_primary_stress()) return "e";
        if (starts("pt-PT")) return "ɨ";
        if (starts("pt-BR") && is_last_word_letter()) return "ɪ";
        return "e";
    }
    if (s == "o") {
        if (starts("pt-PT") || starts("pt-MZ")) return has_primary_stress() || has_secondary_stress() ? "o" : "u";
        if (starts("pt-BR") && is_last_word_letter() && !(has_primary_stress() || has_secondary_stress())) return "ʊ";
        return "o";
    }
    if (s == "i" && !has_primary_stress() && !has_secondary_stress()) {
        const std::string& n = next_letter();
        if (in_set(d.VOWEL_CHARS, n) || in_set(d.ACUTE_VOWEL_CHARS, n) || in_set(d.CIRCUM_VOWEL_CHARS, n) ||
            in_set(d.TILDE_VOWEL_CHARS, n)) {
            return "j";
        }
    }
    return base_ipa;
}

std::string CharToken::ipa_for_consonant() const {
    const auto& d = dialect();
    const std::string& s = normalized();
    const std::string& next = next_letter();
    const std::string& prev = prev_letter();
    const std::string& dc = d.dialect_code;
    auto starts = [&](std::string_view p) { return dc.compare(0, p.size(), p) == 0; };

    if (starts("pt-BR")) {
        if (s == "t" && next == "i") return "tʃ";
        if (s == "d" && next == "i") return "dʒ";
        if (s == "t" && next == "e" && suffix() == "e") return "tʃ";
        if (s == "d" && next == "e" && suffix() == "e") return "dʒ";
        if (s == "l" && is_last_word_letter()) return "w";
        if (s == "l") {
            const CharToken* nc = next_char();
            if (nc && nc->is_consonant()) return "w";
        }
    }
    if (s == "c" && o2i::vowels::is_front_vowel(next)) return "s";
    if (s == "g" && o2i::vowels::is_front_vowel(next)) return "ʒ";
    if (s == "r") {
        if (is_first_word_letter() || (!prev.empty() && in_str("lns", prev))) {
            if (starts("pt-BR")) return "h";
            if (starts("pt-PT")) return "ʁ";
            return "r";
        }
        return "ɾ";
    }
    if (s == "s" && is_intervocalic()) return "z";
    if (s == "s" && is_between_consonant_vowel()) {
        const WordToken* parent = parent_word();
        const U32 word = u(parent ? parent->normalized() : "");
        if ((detail::starts_with(word, U"trans") || detail::starts_with(word, U"trâns")) && idx_in_word_ == 4) {
            if (in_set(d.VOWEL_CHARS, next) || in_set(d.ACUTE_VOWEL_CHARS, next)) return "z";
        }
        return "s";
    }
    if (s == "s" && starts("pt-PT")) {
        if (is_last_word_letter()) return "ʃ";
        if (next.size() == 1 && in_str("ptkfsx", next)) return "ʃ";
    }
    if (s == "x") return ipa_for_x();
    if (s == "z" && is_last_word_letter()) return starts("pt-BR") ? "s" : "ʃ";
    if (s == "l" && is_last_word_letter() && starts("pt-PT")) return "ɫ";
    const std::string* mapped = map_get(d.DEFAULT_CHAR2PHONEMES, s);
    return mapped ? *mapped : s;
}

std::string CharToken::ipa_for_x() const {
    const CharToken* pc = prev_char();
    const CharToken* nc = next_char();
    const std::string prev = pc ? pc->normalized() : "";
    const std::string next = nc ? nc->normalized() : "";
    const WordToken* parent = parent_word();
    const U32 word = u(parent ? parent->normalized() : "");

    if (is_first_word_letter()) return "ʃ";
    if (is_last_word_letter()) return "ks";
    if (is_intervocalic()) {
        if (detail::starts_with(word, U"hexa") && idx_in_word_ == 2) return "gz";
        if (detail::starts_with(word, U"próxim") && idx_in_word_ == 3) return "s";
        if (prev == "e" && in_str("aeiouáéíóú", next)) return "z";
        if (in_set(dialect().ACUTE_VOWEL_CHARS, prev) || prev == "e") {
            if (prev == "ú") return "ʃ";
            return "ks";
        }
        return "ʃ";
    }
    return "ʃ";
}

const std::string& CharToken::ipa() const {
    if (!ipa_) {
        if (is_punct()) {
            const std::string* p = map_get(dialect().PUNCT2IPA, normalized());
            ipa_ = p ? *p : dialect().HIATUS_TOKEN;
        } else if (is_silent()) {
            ipa_ = "";
        } else if (is_vowel()) {
            ipa_ = ipa_for_vowel();
        } else {
            ipa_ = ipa_for_consonant();
        }
    }
    return *ipa_;
}

Features CharToken::features() const {
    return {
        {"text", normalized()},
        {"ipa", ipa()},
        {"is_first_letter", is_first_word_letter()},
        {"is_last_letter", is_last_word_letter()},
        {"is_punct", is_punct()},
        {"is_vowel", is_vowel()},
        {"is_semivowel", is_semivowel()},
        {"is_nasal_vowel", is_nasal_vowel()},
        {"is_open_vowel", is_open_vowel()},
        {"is_closed_vowel", is_closed_vowel()},
        {"is_consonant", is_consonant()},
        {"is_foreign", is_foreign()},
        {"is_silent", is_silent()},
        {"is_intervocalic", is_intervocalic()},
        {"is_between_consonant_vowel", is_between_consonant_vowel()},
        {"is_between_vowel_consonant", is_between_vowel_consonant()},
        {"has_diacritics", has_diacritics()},
        {"has_primary_stress", has_primary_stress()},
        {"has_secondary_stress", has_secondary_stress()},
        {"idx_in_syllable", idx_in_syllable()},
        {"is_nucleus", is_nucleus()},
        {"is_onset", is_onset()},
        {"is_coda", is_coda()},
        {"manner_of_articulation", fv(manner_of_articulation())},
        {"place_of_articulation", fv(place_of_articulation())},
        {"voicing", fv(voicing())},
        {"vowel_height", fv(vowel_height())},
        {"vowel_backness", fv(vowel_backness())},
        {"vowel_roundedness", fv(vowel_roundedness())},
        {"is_front_vowel", is_front_vowel()},
        {"is_back_vowel", is_back_vowel()},
        {"is_rounded_vowel", is_rounded_vowel()},
        {"is_plosive", is_plosive()},
        {"is_fricative", is_fricative()},
        {"is_nasal_consonant", is_nasal_consonant()},
        {"is_liquid", is_liquid()},
        {"is_sibilant", is_sibilant()},
        {"is_rhotic", is_rhotic()},
        {"is_sonorant", is_sonorant()},
        {"is_obstruent", is_obstruent()},
    };
}

std::string CharToken::repr() const { return "CharToken('" + surface_ + "' → [" + ipa() + "])"; }

// ── GraphemeToken ────────────────────────────────────────────────────────────

GraphemeToken::GraphemeToken(std::string surface, long grapheme_idx, long syllable_idx, WordToken* parent_word)
    : surface_(std::move(surface)), grapheme_idx_(grapheme_idx), syllable_idx_(syllable_idx),
      parent_word_(parent_word) {
    long i = 0;
    for (const auto& c : code_points(surface_)) characters_.push_back(std::make_unique<CharToken>(c, i++, this));
}

const DialectInventory& GraphemeToken::dialect() const {
    if (!parent_word_) throw AttributeError("'NoneType' object has no attribute 'dialect'");
    return parent_word_->dialect();
}

const std::string& GraphemeToken::normalized() const {
    if (!normalized_) normalized_ = s8(detail::lower(u(surface_)));
    return *normalized_;
}

const CharToken& GraphemeToken::first_char() const { return *py_index(characters_, 0); }
const CharToken& GraphemeToken::last_char() const { return *py_index(characters_, -1); }

Sentence* GraphemeToken::parent_sentence() const { return parent_word_ ? parent_word_->parent_sentence() : nullptr; }

std::optional<std::string> GraphemeToken::parent_syllable() const {
    if (!parent_word_ || syllable_idx_ < 0) return std::nullopt;
    if (syllable_idx_ >= parent_word_->n_syllables()) return std::nullopt;
    return parent_word_->syllables()[static_cast<size_t>(syllable_idx_)];
}

const std::string& GraphemeToken::prefix() const {
    if (!prefix_) {
        std::string out;
        if (parent_word_) {
            for (const auto& g : parent_word_->graphemes()) {
                if (g->grapheme_idx_ < grapheme_idx_) out += g->normalized();
            }
        }
        prefix_ = out;
    }
    return *prefix_;
}

const std::string& GraphemeToken::suffix() const {
    if (!suffix_) {
        std::string out;
        if (parent_word_) {
            for (const auto& g : parent_word_->graphemes()) {
                if (g->grapheme_idx_ > grapheme_idx_) out += g->normalized();
            }
        }
        suffix_ = out;
    }
    return *suffix_;
}

const GraphemeToken* GraphemeToken::prev_grapheme() const {
    if (grapheme_idx_ == 0 || !parent_word_) return nullptr;
    return py_index(parent_word_->graphemes(), grapheme_idx_ - 1).get();
}

const GraphemeToken* GraphemeToken::next_grapheme() const {
    if (!parent_word_) return nullptr;
    if (grapheme_idx_ >= static_cast<long>(parent_word_->graphemes().size()) - 1) return nullptr;
    return py_index(parent_word_->graphemes(), grapheme_idx_ + 1).get();
}

std::optional<std::string> GraphemeToken::prev_syllable() const {
    if (syllable_idx_ == 0) return std::nullopt;
    const auto own = parent_syllable();
    if (!own || own->empty()) return std::nullopt;
    return py_index(parent_word_->normalized_syllables(), syllable_idx_ - 1);
}

std::optional<std::string> GraphemeToken::next_syllable() const {
    if (syllable_idx_ == -1) return std::nullopt;
    const auto own = parent_syllable();
    if (!own || own->empty()) return std::nullopt;
    const auto& norm = parent_word_->normalized_syllables();
    if (syllable_idx_ >= static_cast<long>(norm.size()) - 1) return std::nullopt;
    return py_index(norm, syllable_idx_ + 1);
}

bool GraphemeToken::is_archaism() const {
    const std::string& s = normalized();
    if (in_str(s, "ü")) return true;
    const auto& d = dialect();
    for (const auto& c : d.GRAVE_VOWEL_CHARS) {
        if (c != "à" && in_str(s, c)) return true;
    }
    if (s == "êle") return true;
    if (s == "ph") return true;
    if (const dialects::StrSet* words = d.ARCHAIC_MUTE_P.find(s)) return words->count(parent_word_->normalized()) != 0;
    return false;
}

bool GraphemeToken::is_nasal() const {
    const std::string& s = normalized();
    const auto& d = dialect();
    if (d.NASAL_DIGRAPHS.contains(s)) return true;
    for (const auto& c : d.TILDE_VOWEL_CHARS) {
        if (in_str(s, c)) return true;
    }
    return false;
}

bool GraphemeToken::is_vocalic_hiatus() const {
    if (!is_vowel_grapheme()) return false;
    for (const GraphemeToken* neighbor : {prev_grapheme(), next_grapheme()}) {
        if (neighbor && neighbor->is_vowel_grapheme() && neighbor->syllable_idx_ != syllable_idx_) return true;
    }
    return false;
}

bool GraphemeToken::is_diphthong() const {
    if (!is_diphthong_) {
        const std::string& s = normalized();
        if (s == "ue" && parent_word_ && parent_word_->normalized() == "quem") {
            is_diphthong_ = false;
        } else if (s == "ue" && !parent_word_) {
            throw AttributeError("'NoneType' object has no attribute 'normalized'");
        } else {
            is_diphthong_ = dialect().DIPHTHONG2IPA.contains(s);
        }
    }
    return *is_diphthong_;
}

bool GraphemeToken::is_triphthong() const { return dialect().TRIPHTHONG2IPA.contains(normalized()); }

bool GraphemeToken::is_falling_diphthong() const {
    if (!is_diphthong()) return false;
    const auto& d = dialect();
    if (d.dialect_code.compare(0, 5, "pt-BR") == 0) {
        for (const auto& [key, value] : d.PTBR_DIPHTHONGS) {
            if (value == normalized()) return true;
        }
    }
    return !in_set(d.SEMIVOWEL_CHARS, first_char().normalized());
}

bool GraphemeToken::is_rising_diphthong() const {
    if (!is_diphthong()) return false;
    return in_set(dialect().SEMIVOWEL_CHARS, first_char().normalized());
}

bool GraphemeToken::is_nasal_diphthong() const {
    if (!is_diphthong()) return false;
    return in_set(dialect().TILDE_VOWEL_CHARS, first_char().normalized());
}

bool GraphemeToken::is_oral_diphthong() const { return is_diphthong() && !is_nasal_diphthong(); }
bool GraphemeToken::is_digraph() const { return dialect().DIGRAPH2IPA.contains(normalized()); }
bool GraphemeToken::is_foreign_digraph() const { return dialect().FOREIGN_DIGRAPH2IPA.contains(normalized()); }
bool GraphemeToken::is_trigraph() const { return dialect().TRIGRAM2IPA.contains(normalized()); }
bool GraphemeToken::is_consonant_hiatus() const { return dialect().HETEROSYLLABIC_CLUSTERS.contains(normalized()); }

bool GraphemeToken::is_vowel_grapheme() const {
    if (!is_vowel_grapheme_) {
        bool all = true;
        for (const auto& c : characters_) {
            if (!c->is_vowel()) {
                all = false;
                break;
            }
        }
        is_vowel_grapheme_ = all;
    }
    return *is_vowel_grapheme_;
}

bool GraphemeToken::is_consonant_grapheme() const {
    for (const auto& c : characters_) {
        if (!c->is_consonant()) return false;
    }
    return true;
}

long GraphemeToken::idx_in_syllable() const { return first_char().idx_in_syllable(); }

std::string GraphemeToken::syllable_position() const {
    if (!syllable_position_) {
        if (is_vowel_grapheme() || is_diphthong() || is_triphthong() || is_nasal()) {
            syllable_position_ = "nucleus";
        } else {
            syllable_position_ = first_char().is_coda() ? "coda" : "onset";
        }
    }
    return *syllable_position_;
}

long GraphemeToken::phonological_weight() const {
    bool all_silent = true;
    for (const auto& c : characters_) {
        if (!c->is_silent()) {
            all_silent = false;
            break;
        }
    }
    if (all_silent) return 0;
    if (is_diphthong()) return 2;
    U32 text = u(ipa());
    for (const char32_t* marker : {U"ˈ", U"ˌ", U"ː", U"·"}) text = detail::replace(text, marker, U"");
    long n = 0;
    for (char32_t c : detail::nfd(text)) {
        if (detail::combining(c) == 0) ++n;
    }
    return n;
}

bool GraphemeToken::has_complex_onset() const {
    if (syllable_position() != "onset" || !is_consonant_grapheme()) return false;
    for (const GraphemeToken* neighbor : {prev_grapheme(), next_grapheme()}) {
        if (neighbor && neighbor->syllable_idx_ == syllable_idx_ && neighbor->is_consonant_grapheme() &&
            neighbor->syllable_position() == "onset") {
            return true;
        }
    }
    return false;
}

bool GraphemeToken::is_palatal() const {
    const std::string* v = map_get(dialect().DIGRAPH2IPA, normalized());
    return v && (*v == "ɲ" || *v == "ʎ");
}

bool GraphemeToken::triggers_palatalization() const {
    if (!(is_vowel_grapheme() || is_diphthong())) return false;
    const CharToken& first = first_char();
    return first.vowel_height() == "high" && first.vowel_backness() == "front";
}

bool GraphemeToken::has_primary_stress() const {
    if (!has_primary_stress_) {
        if (!parent_word_) throw AttributeError("'NoneType' object has no attribute 'n_syllables'");
        if (parent_word_->n_syllables() == 1) {
            has_primary_stress_ = true;
        } else {
            bool marked = false;
            for (const auto& c : characters_) {
                if (in_set(dialect().PRIMARY_STRESS_MARKERS, c->normalized())) {
                    marked = true;
                    break;
                }
            }
            has_primary_stress_ = marked || syllable_idx_ == parent_word_->stressed_syllable_idx();
        }
    }
    return *has_primary_stress_;
}

bool GraphemeToken::has_secondary_stress() const {
    if (has_primary_stress()) return false;
    for (const auto& c : characters_) {
        if (in_set(dialect().SECONDARY_STRESS_MARKERS, c->normalized())) return true;
    }
    return false;
}

const std::string& GraphemeToken::ipa() const {
    if (!ipa_) {
        const std::string& s = normalized();
        const std::string word = parent_word_ ? parent_word_->normalized() : "";
        const auto& d = dialect();
        std::string result;
        if (s == "ui" && word == "muito") {
            result = "ũj";
        } else if (const std::string* tetra = map_get(d.TETRAGRAM2IPA, s)) {
            result = *tetra;
        } else if (const std::string* tri = map_get(d.TRIGRAM2IPA, s)) {
            if (in_str("qg", py_char(s, 0)) && py_char(s, 1) == "u") {
                std::string onset;
                for (size_t i = 0; i < characters_.size() && i < 2; ++i) onset += characters_[i]->ipa();
                const bool stressed = has_primary_stress() || has_secondary_stress();
                if (stressed && !parent_word_->is_function_word()) {
                    result = onset + py_index(characters_, 2)->ipa();
                } else {
                    result = onset + py_slice(*tri, 1);
                }
            } else {
                result = *tri;
            }
        } else if (const std::string* nasal = map_get(d.NASAL_DIGRAPHS, s)) {
            result = *nasal;
        } else if (const std::string* diph = map_get(d.DIPHTHONG2IPA, s)) {
            result = *diph;
        } else if (const std::string* di = map_get(d.DIGRAPH2IPA, s)) {
            result = *di;
        } else if (const std::string* het = map_get(d.HETEROSYLLABIC_CLUSTERS, s)) {
            result = *het;
        } else {
            for (const auto& c : characters_) result += c->ipa();
        }
        ipa_ = result;
    }
    return *ipa_;
}

Features GraphemeToken::features() const {
    Features feats = {
        {"n_chars", n_chars()},
        {"text", normalized()},
        {"ipa", ipa()},
        {"parent_syllable", fv(parent_syllable())},
        {"prev_syllable", fv(prev_syllable())},
        {"next_syllable", fv(next_syllable())},
        {"is_archaism", is_archaism()},
        {"is_nasal", is_nasal()},
        {"is_digraph", is_digraph()},
        {"is_trigraph", is_trigraph()},
        {"is_foreign_digraph", is_foreign_digraph()},
        {"is_consonant_hiatus", is_consonant_hiatus()},
        {"is_diphthong", is_diphthong()},
        {"is_triphthong", is_triphthong()},
        {"is_falling_diphthong", is_falling_diphthong()},
        {"is_rising_diphthong", is_rising_diphthong()},
        {"is_nasal_diphthong", is_nasal_diphthong()},
        {"is_oral_diphthong", is_oral_diphthong()},
        {"is_vocalic_hiatus", is_vocalic_hiatus()},
        {"is_vowel_grapheme", is_vowel_grapheme()},
        {"is_consonant_grapheme", is_consonant_grapheme()},
        {"idx_in_syllable", idx_in_syllable()},
        {"syllable_position", syllable_position()},
        {"phonological_weight", phonological_weight()},
        {"has_complex_onset", has_complex_onset()},
        {"is_onset_cluster", is_onset_cluster()},
        {"is_palatal", is_palatal()},
        {"triggers_palatalization", triggers_palatalization()},
        {"has_primary_stress", has_primary_stress()},
        {"has_secondary_stress", has_secondary_stress()},
    };
    FeatureDict dict(std::move(feats));
    for (const auto& c : characters_) {
        const std::string prefix = "char_" + std::to_string(c->char_idx()) + "_";
        for (auto& [k, v] : c->features()) dict.set(prefix + k, v);
    }
    return dict.take();
}

std::string GraphemeToken::repr() const { return "GraphemeToken('" + surface_ + "' → [" + ipa() + "])"; }

// ── WordToken ────────────────────────────────────────────────────────────────

WordToken::WordToken(std::string surface, long word_idx, std::vector<std::string> syllables, Sentence* parent_sentence,
                     DialectPtr dialect, std::optional<long> chunk_idx)
    : surface_(std::move(surface)), word_idx_(word_idx), syllables_(std::move(syllables)),
      parent_sentence_(parent_sentence), dialect_(or_default(std::move(dialect))), chunk_idx_(chunk_idx) {
    if (syllables_.empty()) syllables_ = silabificador::syllabify(normalized());
    tokenize_graphemes();
    compute_indices();
}

std::vector<std::pair<long, long>> WordToken::build_char_to_syllable_map(const std::vector<std::string>& syllables) {
    std::vector<std::pair<long, long>> out;
    long pos = 0;
    for (size_t i = 0; i < syllables.size(); ++i) {
        for (long k = py_len(syllables[i]); k > 0; --k) out.emplace_back(pos++, static_cast<long>(i));
    }
    return out;
}

void WordToken::tokenize_graphemes() {
    const auto normalized_syllables = normalize_syllables();
    const auto& tokenizer = phonetok(dialect_->dialect_code, dialect_->GRAPHEME_INVENTORY);
    for (size_t syl_idx = 0; syl_idx < normalized_syllables.size(); ++syl_idx) {
        for (const auto& token : tokenizer.tokenize(normalized_syllables[syl_idx])) {
            if (token.kind != o2i::TokenKind::GRAPHEME && token.kind != o2i::TokenKind::UNKNOWN &&
                token.kind != o2i::TokenKind::DIGIT && token.kind != o2i::TokenKind::PUNCTUATION) {
                continue;
            }
            graphemes_.push_back(std::make_unique<GraphemeToken>(token.grapheme, static_cast<long>(graphemes_.size()),
                                                                 static_cast<long>(syl_idx), this));
        }
    }
}

std::vector<std::string> WordToken::normalize_syllables() const {
    std::vector<U32> norm;
    for (const auto& s : syllables_) norm.push_back(u(s));
    for (size_t idx = 0; idx + 1 < norm.size(); ++idx) {
        const U32 current = norm[idx];
        const U32 next = norm[idx + 1];
        for (char32_t consonant : {U'r', U's', U'f', U'l'}) {
            if (!current.empty() && current.back() == consonant && !next.empty() && next.front() == consonant) {
                norm[idx] = current.substr(0, current.size() - 1);
                norm[idx + 1] = U32(1, consonant) + next;
                break;
            }
        }
    }
    std::vector<std::string> out;
    for (const auto& s : norm) out.push_back(s8(s));
    return out;
}

void WordToken::compute_indices() {
    long char_idx_in_word = 0;
    for (auto& g : graphemes_) {
        g->idx_in_word_ = char_idx_in_word;
        g->idx_in_sentence_ = idx_in_sentence_ + char_idx_in_word;
        for (auto& c : g->characters_) {
            c->idx_in_word_ = char_idx_in_word;
            c->idx_in_sentence_ = idx_in_sentence_ + char_idx_in_word;
            ++char_idx_in_word;
        }
    }
}

const std::string& WordToken::normalized() const {
    if (!normalized_) normalized_ = s8(detail::strip_ws(detail::lower(u(surface_))));
    return *normalized_;
}

const std::vector<std::string>& WordToken::normalized_syllables() const {
    if (!normalized_syllables_) normalized_syllables_ = normalize_syllables();
    return *normalized_syllables_;
}

bool WordToken::is_archaic() const { return in_set(dialect_->ARCHAIC_WORDS, normalized()); }

const WordToken* WordToken::prev_word() const {
    if (word_idx_ == 0 || !parent_sentence_) return nullptr;
    return py_index(parent_sentence_->words(), word_idx_ - 1).get();
}

const WordToken* WordToken::next_word() const {
    if (word_idx_ == -1 || !parent_sentence_) return nullptr;
    if (word_idx_ >= static_cast<long>(parent_sentence_->words().size()) - 1) return nullptr;
    return py_index(parent_sentence_->words(), word_idx_ + 1).get();
}

long WordToken::stressed_syllable_idx() const {
    if (!stressed_syllable_idx_) stressed_syllable_idx_ = detect_stress_position(normalized(), syllables_, *dialect_);
    return *stressed_syllable_idx_;
}

std::vector<const WordToken*> WordToken::hyphen_group() const {
    if (!chunk_idx_ || !parent_sentence_) return {this};
    std::vector<const WordToken*> out;
    for (const auto& w : parent_sentence_->words()) {
        if (w->chunk_idx_ == chunk_idx_) out.push_back(w.get());
    }
    return out;
}

bool WordToken::is_function_word() const {
    if (!is_function_word_) {
        if (n_syllables() != 1) {
            is_function_word_ = false;
        } else {
            const o2i::LanguageSpec* rules = spec_stress_rules(dialect_->dialect_code);
            is_function_word_ = rules && is_cliticless(*rules, normalized());
        }
    }
    return *is_function_word_;
}

bool WordToken::is_hyphen_clitic() const {
    if (!is_hyphen_clitic_) {
        if (hyphen_group().size() == 1) {
            is_hyphen_clitic_ = false;
        } else {
            const o2i::LanguageSpec* rules = spec_stress_rules(dialect_->dialect_code);
            is_hyphen_clitic_ = rules && is_cliticless(*rules, normalized());
        }
    }
    return *is_hyphen_clitic_;
}

std::string WordToken::stress_token() const {
    if (is_hyphen_clitic() || is_function_word()) return "";
    std::vector<const WordToken*> stressed;
    for (const WordToken* w : hyphen_group()) {
        if (!w->is_hyphen_clitic()) stressed.push_back(w);
    }
    if (py_index(stressed, -1) == this) return dialect_->PRIMARY_STRESS_TOKEN;
    return dialect_->SECONDARY_STRESS_TOKEN;
}

std::string WordToken::stress_pattern() const {
    if (n_syllables() == 1) return "monosyllable";
    const long position_from_end = n_syllables() - stressed_syllable_idx();
    if (position_from_end == 1) return "oxytone";
    if (position_from_end == 2) return "paroxytone";
    if (position_from_end == 3) return "proparoxytone";
    return "irregular";
}

bool WordToken::has_diphthongs() const {
    for (const auto& g : graphemes_) {
        if (g->is_diphthong()) return true;
    }
    return false;
}

bool WordToken::has_nasal_sounds() const {
    for (const auto& g : graphemes_) {
        if (g->is_nasal()) return true;
    }
    return false;
}

bool WordToken::has_palatal_sounds() const {
    for (const auto& g : graphemes_) {
        if (g->is_palatal()) return true;
    }
    return false;
}

bool WordToken::has_consonant_clusters() const {
    for (const auto& g : graphemes_) {
        if (g->has_complex_onset()) return true;
    }
    return false;
}

std::string WordToken::syllable_structure_pattern() const {
    std::string out;
    bool first = true;
    for (const auto& syllable : normalized_syllables()) {
        if (!first) out += ".";
        first = false;
        for (const auto& ch : code_points(syllable)) out += in_vowel_letters(*dialect_, ch) ? "V" : "C";
    }
    return out;
}

long WordToken::phoneme_count() const {
    long n = 0;
    for (const auto& g : graphemes_) n += g->phonological_weight();
    return n;
}

std::string WordToken::vowel_sequence() const {
    std::vector<U32> parts;
    for (const auto& ch : code_points(normalized())) {
        if (in_vowel_letters(*dialect_, ch)) parts.push_back(u(ch));
    }
    return s8(detail::join(parts, U"."));
}

std::string WordToken::consonant_sequence() const {
    std::vector<U32> parts;
    for (char32_t c : u(normalized())) {
        if (detail::is_alpha(c) && !in_vowel_letters(*dialect_, s8(U32(1, c)))) parts.push_back(U32(1, c));
    }
    return s8(detail::join(parts, U"."));
}

bool WordToken::is_homograph() const { return bifonia::HOMOGRAPHS().contains(normalized()); }

bool WordToken::is_irregular() const { return dialect_->IRREGULAR_WORDS.contains(normalized()); }

const std::string& WordToken::ipa() const {
    if (!ipa_) {
        if (const std::string* irregular = map_get(dialect_->IRREGULAR_WORDS, normalized())) {
            ipa_ = s8(detail::replace(u(*irregular), u(dialect_->PRIMARY_STRESS_TOKEN), u(stress_token())));
            return *ipa_;
        }
        std::vector<std::string> syllable_strings(syllables_.size());
        for (const auto& g : graphemes_) {
            const long syl_idx = g->syllable_idx();
            if (0 <= syl_idx && syl_idx < static_cast<long>(syllable_strings.size())) {
                const std::string& gi = g->ipa();
                if (!gi.empty()) syllable_strings[static_cast<size_t>(syl_idx)] += gi;
            }
        }
        const long stressed = stressed_syllable_idx();
        if (0 <= stressed && stressed < static_cast<long>(syllable_strings.size())) {
            syllable_strings[static_cast<size_t>(stressed)] = stress_token() + syllable_strings[static_cast<size_t>(stressed)];
        }
        std::string out;
        for (size_t i = 0; i < syllable_strings.size(); ++i) {
            if (i) out += dialect_->HIATUS_TOKEN;
            out += syllable_strings[i];
        }
        ipa_ = out;
    }
    return *ipa_;
}

Features WordToken::features() const {
    Features feats = {
        {"text", normalized()},
        {"ipa", ipa()},
        {"syllables", syllables_},
        {"n_syllables", n_syllables()},
        {"idx_in_sentence", idx_in_sentence()},
        {"stressed_syllable_idx", stressed_syllable_idx()},
        {"stress_pattern", stress_pattern()},
        {"syllable_structure_pattern", syllable_structure_pattern()},
        {"phoneme_count", phoneme_count()},
        {"vowel_sequence", vowel_sequence()},
        {"consonant_sequence", consonant_sequence()},
        {"has_diphthongs", has_diphthongs()},
        {"has_nasal_sounds", has_nasal_sounds()},
        {"has_palatal_sounds", has_palatal_sounds()},
        {"has_consonant_clusters", has_consonant_clusters()},
        {"is_homograph", is_homograph()},
        {"is_irregular", is_irregular()},
        {"is_archaic", is_archaic()},
    };
    FeatureDict dict(std::move(feats));
    for (const auto& g : graphemes_) {
        const std::string prefix = "graph_" + std::to_string(g->grapheme_idx()) + "_";
        for (auto& [k, v] : g->features()) dict.set(prefix + k, v);
    }
    return dict.take();
}

std::string WordToken::repr() const {
    std::string syllables;
    for (size_t i = 0; i < syllables_.size(); ++i) syllables += (i ? "." : "") + syllables_[i];
    return "WordToken('" + surface_ + "' [" + syllables + "] → [" + ipa() + "])";
}

// ── Sentence ─────────────────────────────────────────────────────────────────

Sentence::Sentence(std::string surface, DialectPtr dialect)
    : surface_(std::move(surface)), dialect_(or_default(std::move(dialect))) {
    tokenize();
}

Sentence::Sentence(std::string surface, std::vector<std::unique_ptr<WordToken>> words, DialectPtr dialect)
    : surface_(std::move(surface)), words_(std::move(words)), dialect_(or_default(std::move(dialect))) {
    if (words_.empty()) {
        tokenize();
        return;
    }
    for (auto& w : words_) w->parent_sentence_ = this;
}

void Sentence::tokenize() {
    const U32 norm = u(normalized());
    std::vector<std::pair<U32, long>> word_surfaces;
    long chunk_idx = 0;
    for (const U32& chunk : detail::split_ws(norm)) {
        size_t start = 0;
        while (true) {
            const size_t dash = chunk.find(U'-', start);
            const U32 element = chunk.substr(start, dash == U32::npos ? U32::npos : dash - start);
            if (!element.empty()) word_surfaces.emplace_back(element, chunk_idx);
            if (dash == U32::npos) break;
            start = dash + 1;
        }
        ++chunk_idx;
    }
    size_t char_position = 0;
    long idx = 0;
    for (const auto& [word_surface, chunk] : word_surfaces) {
        const size_t found = char_position > norm.size() ? U32::npos : norm.find(word_surface, char_position);
        const long word_start = found == U32::npos ? -1 : static_cast<long>(found);
        auto word = std::make_unique<WordToken>(s8(word_surface), idx++, std::vector<std::string>{}, this, dialect_,
                                                chunk);
        word->idx_in_sentence_ = word_start;
        words_.push_back(std::move(word));
        const long next = word_start + static_cast<long>(word_surface.size()) + 1;
        char_position = next < 0 ? 0 : static_cast<size_t>(next);
    }
}

const std::string& Sentence::normalized() const {
    if (!normalized_) {
        static const U32 chars = U"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\x0b\x0c";
        const U32 text = detail::strip(detail::lower(u(surface_)), chars);
        normalized_ = number_utils::normalize_numbers(s8(text));
    }
    return *normalized_;
}

const std::string& Sentence::ipa() const {
    if (!ipa_) {
        std::string out;
        for (size_t i = 0; i < words_.size(); ++i) out += (i ? " " : "") + words_[i]->ipa();
        ipa_ = out;
    }
    return *ipa_;
}

Features Sentence::features() const {
    FeatureDict dict({{"n_words", n_words()}, {"n_whitespaces", n_words() - 1}});
    for (const auto& w : words_) {
        const std::string prefix = "word_" + std::to_string(w->word_idx()) + "_";
        for (auto& [k, v] : w->features()) dict.set(prefix + k, v);
    }
    return dict.take();
}

std::string Sentence::repr() const { return "Sentence('" + surface_ + "' → [" + ipa() + "])"; }

void demonstrate_transcription(std::ostream& out, std::string_view text, DialectPtr dialect) {
    Sentence sentence{std::string(text), std::move(dialect)};
    out << "Sentence: " << sentence.surface() << "\n";
    out << "IPA: [" << sentence.ipa() << "]\n\n";
    out << "Words:\n";
    for (const auto& word : sentence.words()) {
        out << word->word_idx() + 1 << ". " << word->surface() << " [" << word->ipa() << "]\n";
        std::string syllables;
        for (size_t i = 0; i < word->syllables().size(); ++i) syllables += (i ? "." : "") + word->syllables()[i];
        out << "   Syllables: " << syllables << "\n";
        out << "   Stress: syllable " << word->stressed_syllable_idx() << "\n";
        std::string graphemes;
        bool first = true;
        for (const auto& g : word->graphemes()) {
            std::string label = g->surface() + "[" + g->ipa() + "]";
            if (g->is_diphthong()) label += "(diphthong)";
            if (g->is_digraph()) label += "(digraph)";
            graphemes += (first ? "" : " ") + label;
            first = false;
        }
        out << "   Graphemes: " << graphemes << "\n\n";
    }
}

}  // namespace tugaphone::tokenizer
