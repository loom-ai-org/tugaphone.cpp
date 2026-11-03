#include "tugaphone/dialects.hpp"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "json.hpp"

namespace tugaphone::dialects {

namespace {

// ── data/dialects.json ──────────────────────────────────────────────────────

void read(StrSet& out, const json::Value& v) {
    out.clear();
    for (const auto& item : v.items) out.insert(item.text);
}
void read(StrMap& out, const json::Value& v) {
    out = StrMap();
    for (const auto& [key, value] : v.members) out.assign(key, value.text);
}
void read(SetMap& out, const json::Value& v) {
    out = SetMap();
    for (const auto& [key, value] : v.members) read(out.setdefault(key), value);
}
void read(StrList& out, const json::Value& v) {
    out.clear();
    for (const auto& item : v.items) out.push_back(item.text);
}
void read(std::string& out, const json::Value& v) { out = v.text; }

std::string kind_of(const std::string&) { return "str"; }
std::string kind_of(const StrSet&) { return "set"; }
std::string kind_of(const StrMap&) { return "dict"; }
std::string kind_of(const SetMap&) { return "dict_of_sets"; }
std::string kind_of(const StrList&) { return "list"; }

struct Tables {
    DialectFields defaults;  // the literal default of each table that has one
    StrSet archaic_words;
    std::string ao1990_code;
    StrMap ao1990_irregular;
    // class name -> (field name, entries added to the AO1990 table)
    std::map<std::string, std::vector<std::pair<std::string, StrMap>>> merges;
};

const Tables& tables() {
    static const Tables t = [] {
        const json::Value doc = json::loads(detail::read_file(detail::data_path("dialects.json")));
        Tables out;
        // The field list must be the one this code was written for.
        const auto& fields = doc.at("fields").items;
        size_t i = 0;
        DialectFields probe;
#define TUGAPHONE_CHECK_FIELD(type, name)                                                     \
    if (i >= fields.size() || fields[i].at("name").text != #name ||                          \
        fields[i].at("kind").text != kind_of(probe.name))                                     \
        throw ValueError("dialects.json: field " + std::to_string(i) + " is not " #name);    \
    ++i;
        TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_CHECK_FIELD)
#undef TUGAPHONE_CHECK_FIELD
        if (i != fields.size()) throw ValueError("dialects.json: extra fields");

        const json::Value& defaults = doc.at("defaults");
#define TUGAPHONE_READ_DEFAULT(type, name) \
    if (const json::Value* v = defaults.find(#name)) read(out.defaults.name, *v);
        TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_READ_DEFAULT)
#undef TUGAPHONE_READ_DEFAULT
        read(out.archaic_words, doc.at("ARCHAIC_WORDS"));
        out.ao1990_code = doc.at("AO1990").at("dialect_code").text;
        read(out.ao1990_irregular, doc.at("AO1990").at("IRREGULAR_WORDS"));
        for (const auto& [cls, merged] : doc.at("merges").members) {
            auto& entries = out.merges[cls];
            for (const auto& [field, adds] : merged.members) {
                StrMap m;
                read(m, adds);
                entries.emplace_back(field, std::move(m));
            }
        }
        return out;
    }();
    return t;
}

// ── __post_init__ ───────────────────────────────────────────────────────────

// Python's `not table`. The string fields keep what they are given.
template <class T>
bool is_empty(const T& v) {
    return v.empty();
}
bool is_empty(const std::string&) { return false; }

StrSet set_union(std::initializer_list<const StrSet*> parts) {
    StrSet out;
    for (const StrSet* p : parts) out.insert(p->begin(), p->end());
    return out;
}

std::string repeat(const std::string& s, int n) {
    std::string out;
    for (int k = 0; k < n; ++k) out += s;
    return out;
}

const std::string kAsciiLowercase = "abcdefghijklmnopqrstuvwxyz";
const std::string kPunctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

void post_init(DialectFields& f) {
    const DialectFields& d = tables().defaults;
    // Each table that has a literal default takes it when it is empty. The
    // derived tables come after their sources, in the Python order.
#define TUGAPHONE_FILL_LITERAL(type, name) \
    if (is_empty(f.name) && !is_empty(d.name)) f.name = d.name;
    TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_FILL_LITERAL)
#undef TUGAPHONE_FILL_LITERAL

    if (f.PRIMARY_STRESS_MARKERS.empty()) f.PRIMARY_STRESS_MARKERS = set_union({&f.ACUTE_VOWEL_CHARS, &f.TILDE_VOWEL_CHARS});
    if (f.SECONDARY_STRESS_MARKERS.empty()) {
        f.SECONDARY_STRESS_MARKERS =
            set_union({&f.GRAVE_VOWEL_CHARS, &f.CIRCUM_VOWEL_CHARS, &f.TREMA_VOWEL_CHARS});
    }
    if (f.ALL_VOWEL_CHARS.empty()) {
        f.ALL_VOWEL_CHARS = set_union({&f.VOWEL_CHARS, &f.ACUTE_VOWEL_CHARS, &f.GRAVE_VOWEL_CHARS,
                                       &f.CIRCUM_VOWEL_CHARS, &f.TREMA_VOWEL_CHARS});
    }
    if (f.VOWEL_HEIGHT.empty()) {
        for (const auto& p : f.CLOSED_VOWELS) f.VOWEL_HEIGHT.assign(p, "high");
        for (const auto& p : f.SEMI_CLOSED_VOWELS) f.VOWEL_HEIGHT.assign(p, "mid-high");
        for (const auto& p : f.SEMI_OPEN_VOWELS) f.VOWEL_HEIGHT.assign(p, "mid-low");
        for (const auto& p : f.OPEN_VOWELS) f.VOWEL_HEIGHT.assign(p, "low");
        f.VOWEL_HEIGHT.assign("y", "high");
        f.VOWEL_HEIGHT.assign("ə", "mid-low");
    }
    if (f.PUNCT2IPA.empty()) {
        f.PUNCT2IPA.assign("-", f.HIATUS_TOKEN);
        f.PUNCT2IPA.assign(",", f.HIATUS_TOKEN);
        f.PUNCT2IPA.assign(";", repeat(f.HIATUS_TOKEN, 2));
        f.PUNCT2IPA.assign(".", repeat(f.HIATUS_TOKEN, 3));
        f.PUNCT2IPA.assign("!", f.PRIMARY_STRESS_TOKEN + f.HIATUS_TOKEN);
        f.PUNCT2IPA.assign("?", "↗" + f.HIATUS_TOKEN);
    }
    if (f.DIPHTHONG2IPA.empty()) {
        for (const auto& [k, v] : f.RISING_ORAL_DIPHTHONGS) f.DIPHTHONG2IPA.assign(v, k);
        for (const auto& [k, v] : f.FALLING_NASAL_DIPHTHONGS) f.DIPHTHONG2IPA.assign(v, k);
    }
    if (f.GRAPHEME_INVENTORY.empty()) {
        StrSet all = f.ALL_VOWEL_CHARS;
        for (const StrMap* m : {&f.TETRAGRAM2IPA, &f.TRIGRAM2IPA, &f.TRIPHTHONG2IPA, &f.DIGRAPH2IPA,
                                &f.DIPHTHONG2IPA, &f.FOREIGN_DIGRAPH2IPA, &f.HETEROSYLLABIC_CLUSTERS,
                                &f.NASAL_DIGRAPHS}) {
            for (const auto& entry : *m) all.insert(entry.first);
        }
        all.insert(f.HIATUS_PREFIXES.begin(), f.HIATUS_PREFIXES.end());
        for (const auto& entry : f.ARCHAIC_MUTE_P) all.insert(entry.first);
        for (char c : kAsciiLowercase) all.insert(std::string(1, c));
        for (char c : kPunctuation) all.insert(std::string(1, c));
        // sorted(key=lambda x: (-len(x), x)): UTF-8 byte order is code-point order.
        std::vector<std::pair<size_t, std::string>> keyed;
        for (const auto& g : all) keyed.emplace_back(detail::from_utf8(g).size(), g);
        std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (auto& [n, g] : keyed) f.GRAPHEME_INVENTORY.push_back(std::move(g));
    }
}

// ── Equality ────────────────────────────────────────────────────────────────

bool same(const std::string& a, const std::string& b) { return a == b; }
bool same(const StrSet& a, const StrSet& b) { return a == b; }
bool same(const StrList& a, const StrList& b) { return a == b; }
template <class V>
bool same(const tugalex::OrderedMap<V>& a, const tugalex::OrderedMap<V>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& [key, value] : a) {
        const V* other = b.find(key);
        if (other == nullptr || !(*other == value)) return false;
    }
    return true;
}

// ── Subclass helpers ────────────────────────────────────────────────────────

[[noreturn]] void multiple_values(const char* name) {
    throw TypeError(std::string("DialectInventory.__init__() got multiple values for keyword argument '") + name +
                    "'");
}

// The named parameters of a subclass __init__ cannot come through kwargs.
void check_named(const DialectKwargs& kwargs) {
    if (kwargs.dialect_code) multiple_values("dialect_code");
    if (kwargs.IRREGULAR_WORDS) multiple_values("IRREGULAR_WORDS");
}

// {**AO1990.<field>, **adds} into kwargs.<field>; only dict tables merge.
bool merge_into(std::optional<StrMap>& slot, const StrMap& base, const StrMap& adds, const char* name) {
    if (slot) multiple_values(name);
    StrMap merged = base;
    for (const auto& [k, v] : adds) merged.assign(k, v);
    slot = std::move(merged);
    return true;
}
template <class T>
bool merge_into(std::optional<T>&, const T&, const StrMap&, const char*) {
    return false;
}

// Each table the class passes, after checking that kwargs does not pass it too.
void apply_merges(const std::string& cls, DialectKwargs& kwargs) {
    const DialectInventory& ao = AO1990();
    for (const auto& [field, adds] : tables().merges.at(cls)) {
        bool matched = false;
#define TUGAPHONE_MERGE(type, name) \
    if (!matched && field == #name) matched = merge_into(kwargs.name, ao.name, adds, #name);
        TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_MERGE)
#undef TUGAPHONE_MERGE
        if (!matched) throw ValueError("dialects.json: no dict table " + field + " to merge");
    }
}

std::string code_or(const std::optional<std::string>& code, const char* fallback) {
    return code && !code->empty() ? *code : fallback;
}

// IRREGULAR_WORDS or LEXICON.get_ipa_map(region=...)
StrMap irregular_or(std::optional<StrMap> words, const char* region) {
    if (words && !words->empty()) return std::move(*words);
    return LEXICON().get_ipa_map("NOUN", region);
}

// IRREGULAR_WORDS if IRREGULAR_WORDS is not None else LEXICON.get_ipa_map(region=...)
StrMap irregular_if_given(std::optional<StrMap> words, const char* region) {
    if (words) return std::move(*words);
    return LEXICON().get_ipa_map("NOUN", region);
}

DialectKwargs national(const std::string& cls, std::string code, StrMap irregular, DialectKwargs kwargs) {
    check_named(kwargs);
    apply_merges(cls, kwargs);
    kwargs.dialect_code = std::move(code);
    kwargs.IRREGULAR_WORDS = std::move(irregular);
    return kwargs;
}

}  // namespace

// ── DialectInventory ────────────────────────────────────────────────────────

DialectInventory::DialectInventory(const DialectKwargs& kwargs) : DialectInventory("DialectInventory", kwargs) {}

DialectInventory::DialectInventory(std::string type_name, const DialectKwargs& kwargs)
    : type_name_(std::move(type_name)) {
#define TUGAPHONE_TAKE_KWARG(type, name) \
    if (kwargs.name) this->name = *kwargs.name;
    TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_TAKE_KWARG)
#undef TUGAPHONE_TAKE_KWARG
    post_init(*this);
    ARCHAIC_WORDS = tables().archaic_words;
}

bool DialectInventory::operator==(const DialectInventory& other) const {
    if (type_name_ != other.type_name_) return false;
#define TUGAPHONE_SAME_FIELD(type, name) \
    if (!same(this->name, other.name)) return false;
    TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_SAME_FIELD)
#undef TUGAPHONE_SAME_FIELD
    return true;
}

const DialectInventory& AO1990() {
    static const DialectInventory ao = [] {
        DialectKwargs kwargs;
        kwargs.dialect_code = tables().ao1990_code;
        kwargs.IRREGULAR_WORDS = tables().ao1990_irregular;
        return DialectInventory(kwargs);
    }();
    return ao;
}

const tugalex::TugaLexicon& LEXICON() {
    static const tugalex::TugaLexicon lexicon;
    return lexicon;
}

// ── National inventories ────────────────────────────────────────────────────

EuropeanPortuguese::EuropeanPortuguese(std::optional<std::string> dialect_code, std::optional<StrMap> IRREGULAR_WORDS,
                                       DialectKwargs kwargs)
    : EuropeanPortuguese("EuropeanPortuguese", std::move(dialect_code), std::move(IRREGULAR_WORDS),
                         std::move(kwargs)) {}

EuropeanPortuguese::EuropeanPortuguese(std::string type_name, std::optional<std::string> dialect_code,
                                       std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs)
    : DialectInventory(std::move(type_name),
                       national("EuropeanPortuguese", code_or(dialect_code, "pt-PT"),
                                irregular_or(std::move(IRREGULAR_WORDS), "lbx"), std::move(kwargs))) {}

LisbonPortuguese::LisbonPortuguese()
    : EuropeanPortuguese("LisbonPortuguese", std::string("pt-PT-x-lisbon"), std::nullopt, {}) {}

BrazilianPortuguese::BrazilianPortuguese(std::optional<std::string> dialect_code,
                                         std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs)
    : BrazilianPortuguese("BrazilianPortuguese", std::move(dialect_code), std::move(IRREGULAR_WORDS),
                          std::move(kwargs)) {}

BrazilianPortuguese::BrazilianPortuguese(std::string type_name, std::optional<std::string> dialect_code,
                                         std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs)
    : DialectInventory(std::move(type_name),
                       national("BrazilianPortuguese", code_or(dialect_code, "pt-BR"),
                                irregular_or(std::move(IRREGULAR_WORDS), "rjx"), std::move(kwargs))) {}

RioJaneiroPortuguese::RioJaneiroPortuguese()
    : BrazilianPortuguese("RioJaneiroPortuguese", std::string("pt-BR-x-rio-janeiro"), std::nullopt, {}) {}

SaoPauloPortuguese::SaoPauloPortuguese()
    : BrazilianPortuguese("SaoPauloPortuguese", std::string("pt-BR-x-sao-paulo"),
                          LEXICON().get_ipa_map("NOUN", "spx"), {}) {}

AngolanPortuguese::AngolanPortuguese(std::optional<std::string> dialect_code, std::optional<StrMap> IRREGULAR_WORDS,
                                     DialectKwargs kwargs)
    : DialectInventory("AngolanPortuguese",
                       national("AngolanPortuguese", code_or(dialect_code, "pt-AO"),
                                irregular_if_given(std::move(IRREGULAR_WORDS), "lda"), std::move(kwargs))) {}

MozambicanPortuguese::MozambicanPortuguese(std::optional<std::string> dialect_code,
                                           std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs)
    : DialectInventory("MozambicanPortuguese",
                       national("MozambicanPortuguese", code_or(dialect_code, "pt-MZ"),
                                irregular_if_given(std::move(IRREGULAR_WORDS), "mpx"), std::move(kwargs))) {}

TimoresePortuguese::TimoresePortuguese(std::optional<std::string> dialect_code, std::optional<StrMap> IRREGULAR_WORDS,
                                       DialectKwargs kwargs)
    : DialectInventory("TimoresePortuguese",
                       national("TimoresePortuguese", code_or(dialect_code, "pt-TL"),
                                irregular_if_given(std::move(IRREGULAR_WORDS), "dli"), std::move(kwargs))) {}

}  // namespace tugaphone::dialects
