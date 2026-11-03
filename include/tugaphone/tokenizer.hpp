// tugaphone.tokenizer: the rules-only Portuguese orthography -> IPA path and
// its token feature API. A Sentence owns its WordTokens, a WordToken owns its
// GraphemeTokens and a GraphemeToken owns its CharTokens; each token keeps a
// pointer to its parent, so tokens can be neither copied nor moved.
//
// Text is UTF-8. Python's None is std::nullopt (or nullptr for a token). A
// Python cached_property is computed on first use and kept, so an exception
// is raised by the same call that raises it in Python.
#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "tugaphone/dialects.hpp"
#include "tugaphone/errors.hpp"

namespace tugaphone {

// Python's AttributeError: a token without the parent the property needs.
struct AttributeError : Error {
    explicit AttributeError(const std::string& message) : Error("AttributeError", message) {}
};

}  // namespace tugaphone

namespace tugaphone::tokenizer {

using dialects::DialectInventory;
// The dialect a token reads its tables from. A null pointer given to a
// constructor stands for the Python default, a new EuropeanPortuguese().
using DialectPtr = std::shared_ptr<const DialectInventory>;

// detect_stress_position(word, syllables, dialect): the 0-based index of the
// stressed syllable.
int detect_stress_position(std::string_view word, const std::vector<std::string>& syllables,
                           const DialectInventory& dialect);

// PRONOUNCED_QU_GU_U and pronounces_qu_gu_u(word).
const std::set<std::string>& PRONOUNCED_QU_GU_U();
bool pronounces_qu_gu_u(std::string_view word);

// is_grapheme_silent(grapheme, context_before, context_after, word, dialect).
bool is_grapheme_silent(std::string_view grapheme, std::string_view context_before,
                        std::string_view context_after, std::string_view word, const DialectInventory& dialect);

// A value of a features dict: None, bool, int, str or a list of str.
using FeatureValue = std::variant<std::monostate, bool, long, std::string, std::vector<std::string>>;
// A features dict, in the Python insertion order.
using Features = std::vector<std::pair<std::string, FeatureValue>>;

class GraphemeToken;
class WordToken;
class Sentence;

class CharToken {
public:
    // ValueError unless `surface` is one code point; AttributeError when
    // parent_grapheme is null.
    CharToken(std::string surface, long char_idx, GraphemeToken* parent_grapheme);
    CharToken(const CharToken&) = delete;
    CharToken& operator=(const CharToken&) = delete;

    const std::string& surface() const { return surface_; }
    long char_idx() const { return char_idx_; }
    GraphemeToken* parent_grapheme() const { return parent_grapheme_; }

    const DialectInventory& dialect() const;
    const std::string& normalized() const;
    long idx_in_word() const { return idx_in_word_; }
    long idx_in_sentence() const { return idx_in_sentence_; }
    WordToken* parent_word() const;
    Sentence* parent_sentence() const;
    const CharToken* prev_char() const;
    const CharToken* next_char() const;
    std::string prefix() const;
    std::string suffix() const;

    bool is_punct() const;
    bool is_vowel() const;
    bool is_semivowel() const;
    bool is_consonant() const;
    bool is_nasal_vowel() const;
    bool is_foreign() const;
    bool has_diacritics() const;
    bool is_silent() const;
    bool is_open_vowel() const;
    bool is_closed_vowel() const;
    std::optional<std::string> vowel_height() const;
    std::optional<std::string> vowel_backness() const;
    std::optional<std::string> vowel_roundedness() const;
    bool is_front_vowel() const;
    bool is_back_vowel() const;
    bool is_rounded_vowel() const;
    std::optional<std::string> manner_of_articulation() const;
    std::optional<std::string> place_of_articulation() const;
    std::optional<std::string> voicing() const;
    bool is_plosive() const;
    bool is_fricative() const;
    bool is_nasal_consonant() const;
    bool is_liquid() const;
    bool is_sibilant() const;
    bool is_rhotic() const;
    bool is_sonorant() const;
    bool is_obstruent() const;
    std::optional<std::string> parent_syllable() const;
    long idx_in_syllable() const;
    bool is_nucleus() const;
    bool is_onset() const;
    bool is_coda() const;
    bool is_first_word_letter() const;
    bool is_last_word_letter() const;
    bool is_intervocalic() const;
    bool is_between_consonant_vowel() const;
    bool is_between_vowel_consonant() const;
    bool has_primary_stress() const;
    bool has_secondary_stress() const;
    const std::string& ipa() const;
    Features features() const;

    bool operator==(std::string_view other) const { return surface_ == other; }
    std::string repr() const;

private:
    friend class GraphemeToken;
    friend class WordToken;

    std::optional<std::string> vowel_phone() const;
    const std::string& prev_letter() const;
    const std::string& next_letter() const;
    std::string ipa_for_vowel() const;
    std::string ipa_for_consonant() const;
    std::string ipa_for_x() const;

    std::string surface_;
    long char_idx_;
    GraphemeToken* parent_grapheme_;
    long idx_in_word_ = -1;
    long idx_in_sentence_ = -1;

    mutable std::optional<std::string> normalized_, ipa_, prev_letter_, next_letter_;
    mutable std::optional<bool> is_silent_, has_primary_stress_, has_secondary_stress_;
};

class GraphemeToken {
public:
    // The characters are made from `surface`, one CharToken per code point.
    explicit GraphemeToken(std::string surface, long grapheme_idx = 0, long syllable_idx = 0,
                           WordToken* parent_word = nullptr);
    GraphemeToken(const GraphemeToken&) = delete;
    GraphemeToken& operator=(const GraphemeToken&) = delete;

    const std::string& surface() const { return surface_; }
    long grapheme_idx() const { return grapheme_idx_; }
    long syllable_idx() const { return syllable_idx_; }
    const std::vector<std::unique_ptr<CharToken>>& characters() const { return characters_; }
    WordToken* parent_word() const { return parent_word_; }

    // AttributeError when there is no parent word.
    const DialectInventory& dialect() const;
    const std::string& normalized() const;
    long n_chars() const { return static_cast<long>(characters_.size()); }
    // IndexError when the grapheme has no characters.
    const CharToken& first_char() const;
    const CharToken& last_char() const;
    long idx_in_word() const { return idx_in_word_; }
    long idx_in_sentence() const { return idx_in_sentence_; }
    Sentence* parent_sentence() const;
    std::optional<std::string> parent_syllable() const;
    const std::string& prefix() const;
    const std::string& suffix() const;
    const GraphemeToken* prev_grapheme() const;
    const GraphemeToken* next_grapheme() const;
    std::optional<std::string> prev_syllable() const;
    std::optional<std::string> next_syllable() const;

    bool is_archaism() const;
    bool is_nasal() const;
    bool is_vocalic_hiatus() const;
    bool is_diphthong() const;
    bool is_triphthong() const;
    bool is_falling_diphthong() const;
    bool is_rising_diphthong() const;
    bool is_nasal_diphthong() const;
    bool is_oral_diphthong() const;
    bool is_digraph() const;
    bool is_foreign_digraph() const;
    bool is_trigraph() const;
    bool is_consonant_hiatus() const;
    bool is_vowel_grapheme() const;
    bool is_consonant_grapheme() const;
    long idx_in_syllable() const;
    std::string syllable_position() const;
    long phonological_weight() const;
    bool has_complex_onset() const;
    bool is_onset_cluster() const { return has_complex_onset(); }
    bool is_palatal() const;
    bool triggers_palatalization() const;
    bool has_primary_stress() const;
    bool has_secondary_stress() const;
    const std::string& ipa() const;
    Features features() const;

    bool operator==(std::string_view other) const { return surface_ == other; }
    std::string repr() const;

private:
    friend class WordToken;

    std::string surface_;
    long grapheme_idx_;
    long syllable_idx_;
    std::vector<std::unique_ptr<CharToken>> characters_;
    WordToken* parent_word_;
    long idx_in_word_ = -1;
    long idx_in_sentence_ = -1;

    mutable std::optional<std::string> normalized_, ipa_, prefix_, suffix_, syllable_position_;
    mutable std::optional<bool> has_primary_stress_, is_vowel_grapheme_, is_diphthong_;
};

class WordToken {
public:
    // An empty `syllables` is filled by silabificador, as Python's falsy list
    // is. A null dialect is a new EuropeanPortuguese().
    WordToken(std::string surface, long word_idx, std::vector<std::string> syllables = {},
              Sentence* parent_sentence = nullptr, DialectPtr dialect = nullptr,
              std::optional<long> chunk_idx = std::nullopt);
    WordToken(const WordToken&) = delete;
    WordToken& operator=(const WordToken&) = delete;

    const std::string& surface() const { return surface_; }
    long word_idx() const { return word_idx_; }
    const std::vector<std::unique_ptr<GraphemeToken>>& graphemes() const { return graphemes_; }
    const std::vector<std::string>& syllables() const { return syllables_; }
    Sentence* parent_sentence() const { return parent_sentence_; }
    const DialectInventory& dialect() const { return *dialect_; }
    const DialectPtr& dialect_ptr() const { return dialect_; }
    std::optional<long> chunk_idx() const { return chunk_idx_; }

    // The static helper _build_char_to_syllable_map: code-point index ->
    // syllable index.
    static std::vector<std::pair<long, long>> build_char_to_syllable_map(const std::vector<std::string>& syllables);

    const std::string& normalized() const;
    const std::vector<std::string>& normalized_syllables() const;
    long n_syllables() const { return static_cast<long>(syllables_.size()); }
    long idx_in_sentence() const { return idx_in_sentence_; }
    bool is_archaic() const;
    const WordToken* prev_word() const;
    const WordToken* next_word() const;
    long stressed_syllable_idx() const;
    std::vector<const WordToken*> hyphen_group() const;
    bool is_function_word() const;
    bool is_hyphen_clitic() const;
    std::string stress_token() const;
    std::string stress_pattern() const;
    bool has_diphthongs() const;
    bool has_nasal_sounds() const;
    bool has_palatal_sounds() const;
    bool has_consonant_clusters() const;
    std::string syllable_structure_pattern() const;
    long phoneme_count() const;
    std::string vowel_sequence() const;
    std::string consonant_sequence() const;
    bool is_homograph() const;
    bool is_irregular() const;
    const std::string& ipa() const;
    Features features() const;

    bool operator==(std::string_view other) const { return surface_ == other; }
    std::string repr() const;

private:
    friend class Sentence;

    std::vector<std::string> normalize_syllables() const;
    void tokenize_graphemes();
    void compute_indices();

    std::string surface_;
    long word_idx_;
    std::vector<std::unique_ptr<GraphemeToken>> graphemes_;
    std::vector<std::string> syllables_;
    Sentence* parent_sentence_;
    DialectPtr dialect_;
    std::optional<long> chunk_idx_;
    long idx_in_sentence_ = -1;

    mutable std::optional<std::string> normalized_, ipa_;
    mutable std::optional<std::vector<std::string>> normalized_syllables_;
    mutable std::optional<long> stressed_syllable_idx_;
    mutable std::optional<bool> is_hyphen_clitic_, is_function_word_;
};

class Sentence {
public:
    // Splits the normalized text on whitespace and hyphens into WordTokens.
    explicit Sentence(std::string surface, DialectPtr dialect = nullptr);
    // Takes words built by the caller; an empty list is tokenized as above.
    // Each word keeps its own dialect.
    Sentence(std::string surface, std::vector<std::unique_ptr<WordToken>> words, DialectPtr dialect = nullptr);
    Sentence(const Sentence&) = delete;
    Sentence& operator=(const Sentence&) = delete;

    const std::string& surface() const { return surface_; }
    const std::vector<std::unique_ptr<WordToken>>& words() const { return words_; }
    const DialectInventory& dialect() const { return *dialect_; }
    const std::string& normalized() const;
    long n_words() const { return static_cast<long>(words_.size()); }
    const std::string& ipa() const;
    Features features() const;

    bool operator==(std::string_view other) const { return surface_ == other; }
    std::string repr() const;

private:
    void tokenize();

    std::string surface_;
    std::vector<std::unique_ptr<WordToken>> words_;
    DialectPtr dialect_;
    mutable std::optional<std::string> normalized_, ipa_;
};

// demonstrate_transcription(text, dialect): the analysis Python prints, to `out`.
void demonstrate_transcription(std::ostream& out, std::string_view text, DialectPtr dialect = nullptr);

// Python's json.dumps(features, ensure_ascii=False, separators=(",", ":")).
std::string features_json(const Features& features);

}  // namespace tugaphone::tokenizer
