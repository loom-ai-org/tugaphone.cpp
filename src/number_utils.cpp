#include "tugaphone/number_utils.hpp"

#include <tugalex/tugalex.hpp>

#include "detail.hpp"
#include "tugaphone/ovos_number_parser.hpp"

namespace tugaphone::number_utils {

using detail::from_utf8;
using detail::to_utf8;
using detail::U32;
using detail::U32View;

namespace {

const U32 kOrdinalMarks = U"ºª";
// string.whitespace
const U32 kAsciiWhitespace = U" \t\n\r\x0b\x0c";
const U32 kTrailing = U".,;:!?)\"”…";

bool in(const std::set<std::string>& set, U32View word) { return set.count(to_utf8(word)) != 0; }

std::pair<U32, U32> split_trailing(U32View word) {
    size_t i = word.size();
    while (i > 0 && kTrailing.find(word[i - 1]) != U32::npos) --i;
    return {U32(word.substr(0, i)), U32(word.substr(i))};
}

std::optional<BigInt> to_int_u(U32View word) {
    if (detail::contains(word, U'.') || detail::contains(word, U',')) return std::nullopt;
    return py_int(to_utf8(detail::strip(word, kOrdinalMarks + kAsciiWhitespace)));
}

std::optional<double> to_float_u(U32View word) {
    const U32 stripped = detail::strip(word, kOrdinalMarks + kAsciiWhitespace);
    return py_float(to_utf8(detail::replace(stripped, U",", U".", 1)));
}

bool is_float_u(U32View word) { return to_float_u(word).has_value(); }
bool is_int_u(U32View word) { return to_int_u(word).has_value(); }

std::vector<U32> split_on_e(U32View word) {
    std::vector<U32> parts{U32()};
    for (char32_t c : word) {
        if (c == U'e') {
            parts.emplace_back();
        } else {
            parts.back().push_back(c);
        }
    }
    return parts;
}

bool is_scientific_u(U32View word) {
    const auto nums = split_on_e(detail::lower(word));
    if (nums.size() != 2) return false;
    return is_float_u(nums[0]) && is_int_u(nums[1]);
}

bool is_ordinal_u(U32View word, const std::optional<U32>& next_word) {
    if (next_word && (*next_word == U"º" || *next_word == U"ª")) return true;
    return detail::contains(word, U'º') || detail::contains(word, U'ª');
}

std::optional<U32> opt_u(const std::optional<std::string>& s) {
    if (!s) return std::nullopt;
    return from_utf8(*s);
}

std::string number_gender(U32View word, const std::optional<U32>& prev_word, const std::optional<U32>& next_word) {
    // Rule A: an ordinal mark sets the gender.
    if ((next_word && *next_word == U"ª") || detail::contains(word, U'ª')) return "feminine";
    if ((next_word && *next_word == U"º") || detail::contains(word, U'º')) return "masculine";

    // Rule L: a cardinal after a singular label head is invariable.
    if (prev_word && !prev_word->empty() &&
        in(NumberParser::LABEL_HEADS(), detail::lstrip(detail::lower(*prev_word), U"(«\"“"))) {
        const U32 following = next_word ? detail::lower(*next_word) : U32();
        const bool counts_next_word =
            detail::ends_with(following, U"s") && !in(NumberParser::PLURAL_FUNCTION_WORDS(), following);
        if (!counts_next_word) return "masculine";
    }

    const U32 noun = detail::lower(split_trailing(next_word ? *next_word : U32()).first);
    const bool is_word = detail::all_alpha(detail::replace(noun, U"-", U""));
    std::optional<std::string> listed;
    if (is_word && !in(NumberParser::FUNCTION_WORDS(), noun)) {
        listed = tugalex::TugaLexicon::get_noun_gender(to_utf8(noun));
    }
    // Rule N: the counted noun's gender in tugalex.
    if (listed && (*listed == "m" || *listed == "f")) return *listed == "f" ? "feminine" : "masculine";

    // Rule B: an article before the number, when a word follows it.
    if (prev_word && !prev_word->empty() && is_word) {
        if (in(NumberParser::FEMININE_DETERMINERS(), *prev_word)) return "feminine";
        if (in(NumberParser::MASCULINE_DETERMINERS(), *prev_word)) return "masculine";
    }

    // Rules C and D: the ending of the following word.
    if (next_word && !next_word->empty()) {
        if (detail::ends_with(detail::lower(detail::strip(*next_word, U"s")), U"a")) return "feminine";
        if (detail::ends_with(detail::lower(detail::rstrip(*next_word, U"sm")), U"e")) {
            for (U32View ending : {U32View(U"dade"), U32View(U"age"), U32View(U"agem")}) {
                if (detail::ends_with(*next_word, ending)) return "feminine";
            }
        }
    }
    return "masculine";
}

std::string pronounce_word(U32View word, const std::optional<U32>& prev_word, const std::optional<U32>& next_word,
                           std::optional<std::string> gender, std::optional<bool> as_ordinal, bool is_brazilian,
                           const std::optional<std::string>& scale);

std::string pronounce_scientific_u(U32View word, bool is_brazilian, const std::optional<std::string>& scale) {
    if (!is_scientific_u(word)) throw ValueError("word is not scientific notation: '" + to_utf8(word) + "'");
    const auto parts = split_on_e(detail::lower(word));
    const std::string a = pronounce_word(parts[0], std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         is_brazilian, scale);
    const std::string b = pronounce_word(parts[1], std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         is_brazilian, scale);
    return a + " vezes dez elevado a " + b;
}

std::string pronounce_word(U32View word, const std::optional<U32>& prev_word, const std::optional<U32>& next_word,
                           std::optional<std::string> gender, std::optional<bool> as_ordinal, bool is_brazilian,
                           const std::optional<std::string>& scale) {
    if (is_scientific_u(word)) return pronounce_scientific_u(word, is_brazilian, scale);

    const bool is_ord = as_ordinal ? *as_ordinal : is_ordinal_u(word, next_word);
    if (!gender || gender->empty()) gender = number_gender(word, prev_word, next_word);
    const auto gender_enum = *gender == "feminine" ? ovos_number_parser::GrammaticalGender::FEMININE
                                                   : ovos_number_parser::GrammaticalGender::MASCULINE;

    U32 w = detail::replace(detail::replace(word, U" º", U"º"), U" ª", U"ª");
    w = detail::rstrip(detail::strip_ws(w), kOrdinalMarks);
    w = detail::replace(w, U",", U".", 1);
    Number number;
    if (detail::contains(w, U'.')) {
        const auto value = py_float(to_utf8(w));
        if (!value) throw ValueError("could not convert string to float: '" + to_utf8(w) + "'");
        number = *value;
    } else {
        const auto value = py_int(to_utf8(w));
        if (!value) throw ValueError("invalid literal for int() with base 10: '" + to_utf8(w) + "'");
        number = *value;
    }
    const bool use_short_scale = scale && !scale->empty() ? *scale == "short" : is_brazilian;
    return ovos_number_parser::pronounce_number(number, is_brazilian ? "pt-BR" : "pt-PT", 3, use_short_scale, false,
                                                is_ord, std::nullopt, gender_enum);
}

}  // namespace

const std::set<std::string>& NumberParser::LABEL_HEADS() {
    static const std::set<std::string> s = {
        "artigo", "número", "página", "folha", "capítulo", "alínea", "ponto", "parágrafo", "secção", "seção",
        "anexo",  "figura", "tabela", "versículo", "apartamento", "cabine", "sala", "art.", "art.º", "n.º",
        "nº",     "pág.",   "cap."};
    return s;
}

const std::set<std::string>& NumberParser::PLURAL_FUNCTION_WORDS() {
    static const std::set<std::string> s = {"as", "os", "das", "dos", "nas", "nos", "às", "aos", "pelas", "pelos"};
    return s;
}

const std::set<std::string>& NumberParser::FEMININE_DETERMINERS() {
    static const std::set<std::string> s = {"a", "as", "da", "das"};
    return s;
}

const std::set<std::string>& NumberParser::MASCULINE_DETERMINERS() {
    static const std::set<std::string> s = {"o", "os", "do", "dos"};
    return s;
}

const std::set<std::string>& NumberParser::FUNCTION_WORDS() {
    static const std::set<std::string> s = {"a",  "as",  "o",    "os",   "da",    "das",   "do",   "dos",
                                            "na", "nas", "no",   "nos",  "à",     "às",    "ao",   "aos",
                                            "pela", "pelas", "pelo", "pelos", "de", "em",   "e",    "ou",
                                            "que", "para", "por",  "com",  "sem"};
    return s;
}

const std::set<std::string>& NumberParser::RANGE_LINKS() {
    static const std::set<std::string> s = {"a", "à", "às", "ao", "aos"};
    return s;
}

const std::vector<NumberParser::UnitName>& NumberParser::UNIT_NAMES() {
    static const std::vector<UnitName> v = {
        {"%", "por cento", "por cento"},
        {"€", "euro", "euros"},
        {"mm", "milímetro", "milímetros"},
        {"cm", "centímetro", "centímetros"},
        {"m", "metro", "metros"},
        {"km", "quilómetro", "quilómetros"},
        {"km/h", "quilómetro por hora", "quilómetros por hora"},
        {"cm2", "centímetro quadrado", "centímetros quadrados"},
        {"cm²", "centímetro quadrado", "centímetros quadrados"},
        {"m2", "metro quadrado", "metros quadrados"},
        {"m²", "metro quadrado", "metros quadrados"},
        {"km2", "quilómetro quadrado", "quilómetros quadrados"},
        {"km²", "quilómetro quadrado", "quilómetros quadrados"},
        {"cm3", "centímetro cúbico", "centímetros cúbicos"},
        {"cm³", "centímetro cúbico", "centímetros cúbicos"},
        {"m3", "metro cúbico", "metros cúbicos"},
        {"m³", "metro cúbico", "metros cúbicos"},
        {"mg", "miligrama", "miligramas"},
        {"g", "grama", "gramas"},
        {"kg", "quilograma", "quilogramas"},
        {"ml", "mililitro", "mililitros"},
        {"mL", "mililitro", "mililitros"},
        {"cl", "centilitro", "centilitros"},
        {"cL", "centilitro", "centilitros"},
        {"dl", "decilitro", "decilitros"},
        {"dL", "decilitro", "decilitros"},
        {"l", "litro", "litros"},
        {"L", "litro", "litros"},
    };
    return v;
}

std::string NumberParser::unit_name(std::string_view symbol, bool plural, bool is_brazilian) {
    for (const auto& unit : UNIT_NAMES()) {
        if (unit.symbol != symbol) continue;
        const U32 name = from_utf8(plural ? unit.plural : unit.singular);
        return to_utf8(is_brazilian ? detail::replace(name, U"quilómetro", U"quilômetro") : name);
    }
    throw KeyError(std::string(symbol));
}

bool NumberParser::unit_takes_plural(std::optional<double> value, bool is_decimal, bool is_brazilian) {
    if (is_brazilian) {
        if (!value) throw TypeError("'>' not supported between instances of 'NoneType' and 'int'");
        return *value > 1;
    }
    return is_decimal || !value || *value != 1;
}

std::string NumberParser::pronounce_number_word(std::string_view word, std::optional<std::string> prev_word,
                                                std::optional<std::string> next_word, std::optional<std::string> gender,
                                                std::optional<bool> as_ordinal, bool is_brazilian,
                                                std::optional<std::string> scale) {
    return pronounce_word(from_utf8(word), opt_u(prev_word), opt_u(next_word), std::move(gender), as_ordinal,
                          is_brazilian, scale);
}

std::pair<std::string, std::string> NumberParser::split_trailing_punctuation(std::string_view word) {
    const auto [core, trail] = split_trailing(from_utf8(word));
    return {to_utf8(core), to_utf8(trail)};
}

std::optional<BigInt> NumberParser::to_int(std::string_view word) { return to_int_u(from_utf8(word)); }
bool NumberParser::is_int(std::string_view word) { return to_int(word).has_value(); }
std::optional<double> NumberParser::to_float(std::string_view word) { return to_float_u(from_utf8(word)); }
bool NumberParser::is_float(std::string_view word) { return to_float(word).has_value(); }

bool NumberParser::is_decimal(std::string_view word) {
    const U32 w = from_utf8(word);
    if (is_scientific_u(w)) return false;
    return (detail::contains(w, U',') || detail::contains(w, U'.')) && is_float_u(w);
}

bool NumberParser::is_scientific_notation(std::string_view word) { return is_scientific_u(from_utf8(word)); }

std::string NumberParser::pronounce_scientific(std::string_view word, bool is_brazilian,
                                               std::optional<std::string> scale) {
    return pronounce_scientific_u(from_utf8(word), is_brazilian, scale);
}

bool NumberParser::is_ordinal(std::string_view word, std::optional<std::string> next_word) {
    return is_ordinal_u(from_utf8(word), opt_u(next_word));
}

std::string NumberParser::get_number_gender(std::string_view word, std::optional<std::string> prev_word,
                                            std::optional<std::string> next_word) {
    return number_gender(from_utf8(word), opt_u(prev_word), opt_u(next_word));
}

std::string normalize_numbers(std::string_view text, std::string_view lang, bool strict,
                              std::optional<std::string> scale) {
    const U32 lang_u = from_utf8(lang);
    const bool is_brazilian = detail::contains(detail::lower(lang_u), U"pt-br");
    if (!scale) scale = is_brazilian ? "short" : "long";

    const U32 prepared = detail::replace(detail::replace(from_utf8(text), U" º", U"º"), U" ª", U"ª");
    const std::vector<U32> words = detail::split_ws(prepared);
    const long n = static_cast<long>(words.size());
    auto word_at = [&](long i) -> std::optional<U32> {
        if (i < 0 || i >= n) return std::nullopt;
        return words[static_cast<size_t>(i)];
    };
    auto is_number = [](const std::optional<U32>& token) {
        if (!token) return false;
        const U32 core = split_trailing(*token).first;
        return is_int_u(core) || is_float_u(core);
    };
    auto unit_symbol = [](const std::optional<U32>& token) -> std::optional<U32> {
        if (!token) return std::nullopt;
        const U32 symbol = split_trailing(*token).first;
        for (const auto& unit : NumberParser::UNIT_NAMES()) {
            if (from_utf8(unit.symbol) == symbol) return symbol;
        }
        return std::nullopt;
    };

    std::vector<U32> normalized;
    bool skip_unit = false;
    for (long idx = 0; idx < n; ++idx) {
        const U32& word = words[static_cast<size_t>(idx)];
        if (skip_unit) {
            skip_unit = false;
            continue;
        }
        auto [core, trail] = split_trailing(word);
        if (!is_number(core)) {
            normalized.push_back(word);
            continue;
        }
        const std::optional<U32> next_word = word_at(idx + 1);
        const std::optional<U32> prev_word = idx - 1 >= 0 ? word_at(idx - 1) : std::nullopt;
        const std::optional<U32> unit = trail.empty() ? unit_symbol(next_word) : std::nullopt;
        const bool is_fraction = prev_word && *prev_word == U"vírgula" && is_number(idx >= 2 ? word_at(idx - 2) : std::nullopt);

        std::optional<std::string> gender;
        if (unit) {
            gender = "masculine";
        } else if (next_word && *next_word == U"vírgula" && is_number(word_at(idx + 2))) {
            const std::optional<U32> after = word_at(idx + 3);
            gender = unit_symbol(after) ? std::string("masculine") : number_gender(core, prev_word, after);
        } else if (next_word && *next_word == U"vezes" && is_number(word_at(idx + 2))) {
            gender = "masculine";
        } else if (((next_word && in(NumberParser::RANGE_LINKS(), *next_word)) ||
                    (next_word && *next_word == U"e" && prev_word && *prev_word == U"entre")) &&
                   is_number(word_at(idx + 2)) && !is_ordinal_u(core, next_word)) {
            const auto [second, second_trail] = split_trailing(*word_at(idx + 2));
            const std::optional<U32> after =
                (!second_trail.empty() || idx + 3 >= n) ? std::nullopt : word_at(idx + 3);
            gender = unit_symbol(after) ? std::string("masculine") : number_gender(second, next_word, after);
        }
        try {
            std::vector<U32> spelled;
            if (is_fraction && detail::all_digit(core)) {
                const U32 digits = detail::lstrip(core, U"0");
                spelled.assign(core.size() - digits.size(), U"zero");
                core = digits;
            }
            if (!core.empty()) {
                spelled.push_back(from_utf8(
                    pronounce_word(core, prev_word, next_word, gender, std::nullopt, is_brazilian, scale)));
            }
            if (unit) {
                std::optional<double> value;
                if (is_fraction) {
                    const U32 fraction = core.empty() ? U32(U"0") : core;
                    value = py_float(to_utf8(*word_at(idx - 2) + U"." + fraction));
                    if (!value) throw ValueError("could not convert string to float");
                } else {
                    value = to_float_u(core);
                }
                const bool plural = NumberParser::unit_takes_plural(value, is_fraction, is_brazilian);
                if (spelled.empty()) throw IndexError("list index out of range");
                if (*unit != U"%" && (detail::ends_with(spelled.back(), U"ão") ||
                                      detail::ends_with(spelled.back(), U"ões"))) {
                    spelled.push_back(U"de");
                }
                spelled.push_back(from_utf8(NumberParser::unit_name(to_utf8(*unit), plural, is_brazilian)));
                trail = split_trailing(*next_word).second;
                skip_unit = true;
            }
            normalized.push_back(detail::join(spelled, U" ") + trail);
        } catch (const Error&) {
            if (strict) throw;
            normalized.push_back(word);
        }
    }
    return to_utf8(detail::join(normalized, U" "));
}

}  // namespace tugaphone::number_utils
