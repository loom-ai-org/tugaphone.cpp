// tugaphone.number_utils: spell out the numbers of a Portuguese sentence, in
// the gender and the form (cardinal or ordinal) that the context asks for.
#pragma once

#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tugaphone/pynum.hpp"

namespace tugaphone::number_utils {

// The class NumberParser. Every member is static, as every Python member is
// a class attribute or a classmethod. Text is UTF-8. An optional argument
// stands for Python's None; an empty gender or scale string counts as None,
// as Python's `or` makes it.
class NumberParser {
public:
    static constexpr std::string_view ORDINAL_MALE = "º";
    static constexpr std::string_view ORDINAL_FEMALE = "ª";
    static constexpr std::array<std::string_view, 2> ORDINAL_TOKENS = {ORDINAL_MALE, ORDINAL_FEMALE};
    static constexpr std::string_view LEADING_PUNCTUATION = "(“\"'¿¡«";
    static constexpr std::string_view TRAILING_PUNCTUATION = ".,;:!?)\"”»…";

    static const std::set<std::string>& LABEL_HEADS();
    static const std::set<std::string>& PLURAL_FUNCTION_WORDS();
    static const std::set<std::string>& FEMININE_DETERMINERS();
    static const std::set<std::string>& MASCULINE_DETERMINERS();
    static const std::set<std::string>& FUNCTION_WORDS();
    static const std::set<std::string>& RANGE_LINKS();

    struct UnitName {
        std::string symbol, singular, plural;
    };
    // UNIT_NAMES in the Python order.
    static const std::vector<UnitName>& UNIT_NAMES();

    // KeyError for a symbol that is not in UNIT_NAMES.
    static std::string unit_name(std::string_view symbol, bool plural, bool is_brazilian = false);
    // nullopt stands for a None value: Python's None > 1 raises TypeError for
    // pt-BR, and None != 1 is true otherwise.
    static bool unit_takes_plural(std::optional<double> value, bool is_decimal, bool is_brazilian = false);

    static std::string pronounce_number_word(std::string_view word,
                                             std::optional<std::string> prev_word = std::nullopt,
                                             std::optional<std::string> next_word = std::nullopt,
                                             std::optional<std::string> gender = std::nullopt,
                                             std::optional<bool> as_ordinal = std::nullopt,
                                             bool is_brazilian = false,
                                             std::optional<std::string> scale = std::nullopt);

    static std::pair<std::string, std::string> split_trailing_punctuation(std::string_view word);
    static std::optional<BigInt> to_int(std::string_view word);
    static bool is_int(std::string_view word);
    static std::optional<double> to_float(std::string_view word);
    static bool is_float(std::string_view word);
    static bool is_decimal(std::string_view word);
    static bool is_scientific_notation(std::string_view word);
    // ValueError when the word is not scientific notation.
    static std::string pronounce_scientific(std::string_view word, bool is_brazilian = false,
                                            std::optional<std::string> scale = std::nullopt);
    static bool is_ordinal(std::string_view word, std::optional<std::string> next_word = std::nullopt);
    // "masculine" or "feminine".
    static std::string get_number_gender(std::string_view word, std::optional<std::string> prev_word = std::nullopt,
                                         std::optional<std::string> next_word = std::nullopt);
};

// Spell out every numeric token of `text`. With strict, a token that cannot
// be spelled raises the Python exception; without it the token stays.
std::string normalize_numbers(std::string_view text, std::string_view lang = "pt-PT", bool strict = true,
                              std::optional<std::string> scale = std::nullopt);

}  // namespace tugaphone::number_utils
