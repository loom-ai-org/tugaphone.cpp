// tugaphone.dialects: the grapheme and phoneme tables of a Portuguese
// dialect. DialectInventory holds every table; its constructor fills each
// empty table with the default and derives the tables that depend on others,
// as the Python __post_init__ does. The national subclasses start from the
// AO1990 tables and change some entries.
//
// A Python set is a std::set, so it iterates in code-point order where the
// Python set iterates in hash order. VOWEL_HEIGHT is built from sets, so its
// insertion order follows the same order.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <tugalex/tugalex.hpp>

namespace tugaphone::dialects {

using StrSet = std::set<std::string>;
using StrMap = tugalex::OrderedMap<std::string>;
using SetMap = tugalex::OrderedMap<StrSet>;
using StrList = std::vector<std::string>;

// X(type, name) for every dataclass field, in the dataclass order.
#define TUGAPHONE_DIALECT_FIELDS(X)                                     \
    X(std::string, dialect_code)                                        \
    X(std::string, HIATUS_TOKEN)                                        \
    X(std::string, PRIMARY_STRESS_TOKEN)                                \
    X(std::string, SECONDARY_STRESS_TOKEN)                              \
    X(::tugaphone::dialects::StrMap, PUNCT2IPA)                         \
    X(::tugaphone::dialects::StrSet, PUNCT_CHARS)                       \
    X(::tugaphone::dialects::StrSet, VOWEL_CHARS)                       \
    X(::tugaphone::dialects::StrSet, ACUTE_VOWEL_CHARS)                 \
    X(::tugaphone::dialects::StrSet, GRAVE_VOWEL_CHARS)                 \
    X(::tugaphone::dialects::StrSet, CIRCUM_VOWEL_CHARS)                \
    X(::tugaphone::dialects::StrSet, TILDE_VOWEL_CHARS)                 \
    X(::tugaphone::dialects::StrSet, TREMA_VOWEL_CHARS)                 \
    X(::tugaphone::dialects::StrSet, SEMIVOWEL_CHARS)                   \
    X(::tugaphone::dialects::StrSet, FOREIGN_CHARS)                     \
    X(::tugaphone::dialects::StrSet, FRONT_VOWEL_CHARS)                 \
    X(::tugaphone::dialects::StrSet, PRIMARY_STRESS_MARKERS)            \
    X(::tugaphone::dialects::StrSet, SECONDARY_STRESS_MARKERS)          \
    X(::tugaphone::dialects::StrSet, ORAL_VOWELS)                       \
    X(::tugaphone::dialects::StrSet, NASAL_VOWELS)                      \
    X(::tugaphone::dialects::StrSet, CLOSED_VOWELS)                     \
    X(::tugaphone::dialects::StrSet, SEMI_CLOSED_VOWELS)                \
    X(::tugaphone::dialects::StrSet, OPEN_VOWELS)                       \
    X(::tugaphone::dialects::StrSet, SEMI_OPEN_VOWELS)                  \
    X(::tugaphone::dialects::StrSet, ALL_VOWEL_CHARS)                   \
    X(::tugaphone::dialects::StrMap, MANNER_OF_ARTICULATION)            \
    X(::tugaphone::dialects::StrMap, PLACE_OF_ARTICULATION)             \
    X(::tugaphone::dialects::StrSet, VOICED_CONSONANT_CHARS)            \
    X(::tugaphone::dialects::StrSet, VOICELESS_CONSONANT_CHARS)         \
    X(::tugaphone::dialects::StrSet, SIBILANT_CHARS)                    \
    X(::tugaphone::dialects::StrSet, LIQUID_CHARS)                      \
    X(::tugaphone::dialects::StrSet, NASAL_CONSONANT_CHARS)             \
    X(::tugaphone::dialects::StrSet, RHOTIC_CHARS)                      \
    X(::tugaphone::dialects::StrMap, VOWEL_HEIGHT)                      \
    X(::tugaphone::dialects::StrMap, VOWEL_BACKNESS)                    \
    X(::tugaphone::dialects::StrSet, ROUNDED_VOWEL_PHONES)              \
    X(::tugaphone::dialects::StrMap, RISING_ORAL_DIPHTHONGS)            \
    X(::tugaphone::dialects::StrMap, FALLING_NASAL_DIPHTHONGS)          \
    X(::tugaphone::dialects::StrMap, PTBR_DIPHTHONGS)                   \
    X(::tugaphone::dialects::StrMap, NORMALIZED_VOWELS)                 \
    X(::tugaphone::dialects::StrMap, TETRAGRAM2IPA)                     \
    X(::tugaphone::dialects::StrMap, TRIGRAM2IPA)                       \
    X(::tugaphone::dialects::StrMap, TRIPHTHONG2IPA)                    \
    X(::tugaphone::dialects::StrMap, DIPHTHONG2IPA)                     \
    X(::tugaphone::dialects::StrMap, DIGRAPH2IPA)                       \
    X(::tugaphone::dialects::StrMap, NASAL_DIGRAPHS)                    \
    X(::tugaphone::dialects::StrMap, HETEROSYLLABIC_CLUSTERS)           \
    X(::tugaphone::dialects::SetMap, ARCHAIC_MUTE_P)                    \
    X(::tugaphone::dialects::StrMap, FOREIGN_DIGRAPH2IPA)               \
    X(::tugaphone::dialects::StrSet, HIATUS_PREFIXES)                   \
    X(::tugaphone::dialects::StrMap, DEFAULT_CHAR2PHONEMES)             \
    X(::tugaphone::dialects::StrMap, IRREGULAR_WORDS)                   \
    X(::tugaphone::dialects::StrSet, OXYTONE_ENDINGS)                   \
    X(::tugaphone::dialects::StrList, GRAPHEME_INVENTORY)

// The dataclass fields with their dataclass defaults: the four strings have a
// value and every table starts empty.
struct DialectFields {
    std::string dialect_code = "pt-PT";
    std::string HIATUS_TOKEN = "·";
    std::string PRIMARY_STRESS_TOKEN = "ˈ";
    std::string SECONDARY_STRESS_TOKEN = "ˌ";
    StrMap PUNCT2IPA;
    StrSet PUNCT_CHARS, VOWEL_CHARS, ACUTE_VOWEL_CHARS, GRAVE_VOWEL_CHARS, CIRCUM_VOWEL_CHARS, TILDE_VOWEL_CHARS,
        TREMA_VOWEL_CHARS, SEMIVOWEL_CHARS, FOREIGN_CHARS, FRONT_VOWEL_CHARS, PRIMARY_STRESS_MARKERS,
        SECONDARY_STRESS_MARKERS, ORAL_VOWELS, NASAL_VOWELS, CLOSED_VOWELS, SEMI_CLOSED_VOWELS, OPEN_VOWELS,
        SEMI_OPEN_VOWELS, ALL_VOWEL_CHARS;
    StrMap MANNER_OF_ARTICULATION, PLACE_OF_ARTICULATION;
    StrSet VOICED_CONSONANT_CHARS, VOICELESS_CONSONANT_CHARS, SIBILANT_CHARS, LIQUID_CHARS, NASAL_CONSONANT_CHARS,
        RHOTIC_CHARS;
    StrMap VOWEL_HEIGHT, VOWEL_BACKNESS;
    StrSet ROUNDED_VOWEL_PHONES;
    StrMap RISING_ORAL_DIPHTHONGS, FALLING_NASAL_DIPHTHONGS, PTBR_DIPHTHONGS, NORMALIZED_VOWELS, TETRAGRAM2IPA,
        TRIGRAM2IPA, TRIPHTHONG2IPA, DIPHTHONG2IPA, DIGRAPH2IPA, NASAL_DIGRAPHS, HETEROSYLLABIC_CLUSTERS;
    SetMap ARCHAIC_MUTE_P;
    StrMap FOREIGN_DIGRAPH2IPA;
    StrSet HIATUS_PREFIXES;
    StrMap DEFAULT_CHAR2PHONEMES, IRREGULAR_WORDS;
    StrSet OXYTONE_ENDINGS;
    StrList GRAPHEME_INVENTORY;
};

// The keyword arguments of DialectInventory(**kwargs): a field that is set
// is passed, a field that is nullopt is not.
struct DialectKwargs {
#define TUGAPHONE_DIALECT_KWARG(type, name) std::optional<type> name;
    TUGAPHONE_DIALECT_FIELDS(TUGAPHONE_DIALECT_KWARG)
#undef TUGAPHONE_DIALECT_KWARG
};

class DialectInventory : public DialectFields {
public:
    explicit DialectInventory(const DialectKwargs& kwargs = {});

    // Set by __post_init__ outside the dataclass fields.
    StrSet ARCHAIC_WORDS;

    // The Python class name: "DialectInventory", "EuropeanPortuguese", ...
    const std::string& type_name() const { return type_name_; }

    // The dataclass __eq__: the same class and equal fields. Dict and set
    // fields compare without regard to order, as Python's do.
    bool operator==(const DialectInventory& other) const;
    bool operator!=(const DialectInventory& other) const { return !(*this == other); }

protected:
    DialectInventory(std::string type_name, const DialectKwargs& kwargs);

private:
    std::string type_name_;
};

// The national inventories. dialect_code and IRREGULAR_WORDS are the named
// parameters of the Python __init__, so kwargs must leave them unset. A field
// that the subclass passes itself raises TypeError when kwargs sets it too, as
// Python's "got multiple values for keyword argument" does.
class EuropeanPortuguese : public DialectInventory {
public:
    explicit EuropeanPortuguese(std::optional<std::string> dialect_code = std::nullopt,
                                std::optional<StrMap> IRREGULAR_WORDS = std::nullopt, DialectKwargs kwargs = {});

protected:
    EuropeanPortuguese(std::string type_name, std::optional<std::string> dialect_code,
                       std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs);
};

class LisbonPortuguese : public EuropeanPortuguese {
public:
    LisbonPortuguese();
};

class BrazilianPortuguese : public DialectInventory {
public:
    explicit BrazilianPortuguese(std::optional<std::string> dialect_code = std::nullopt,
                                 std::optional<StrMap> IRREGULAR_WORDS = std::nullopt, DialectKwargs kwargs = {});

protected:
    BrazilianPortuguese(std::string type_name, std::optional<std::string> dialect_code,
                        std::optional<StrMap> IRREGULAR_WORDS, DialectKwargs kwargs);
};

class RioJaneiroPortuguese : public BrazilianPortuguese {
public:
    RioJaneiroPortuguese();
};

class SaoPauloPortuguese : public BrazilianPortuguese {
public:
    SaoPauloPortuguese();
};

class AngolanPortuguese : public DialectInventory {
public:
    explicit AngolanPortuguese(std::optional<std::string> dialect_code = std::nullopt,
                               std::optional<StrMap> IRREGULAR_WORDS = std::nullopt, DialectKwargs kwargs = {});
};

class MozambicanPortuguese : public DialectInventory {
public:
    explicit MozambicanPortuguese(std::optional<std::string> dialect_code = std::nullopt,
                                  std::optional<StrMap> IRREGULAR_WORDS = std::nullopt, DialectKwargs kwargs = {});
};

class TimoresePortuguese : public DialectInventory {
public:
    explicit TimoresePortuguese(std::optional<std::string> dialect_code = std::nullopt,
                                std::optional<StrMap> IRREGULAR_WORDS = std::nullopt, DialectKwargs kwargs = {});
};

// The module attributes: the AO1990 base inventory and the shared lexicon.
const DialectInventory& AO1990();
const tugalex::TugaLexicon& LEXICON();

}  // namespace tugaphone::dialects
