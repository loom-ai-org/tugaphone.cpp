// The part of ovos-number-parser that tugaphone uses: pronounce_number for
// European and Brazilian Portuguese, cardinals and ordinals, long and short
// scale. Only "pt" language codes are ported; any other code raises
// NotImplementedError.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "tugaphone/pynum.hpp"

namespace tugaphone::ovos_number_parser {

enum class Scale { SHORT, LONG };
enum class GrammaticalGender { MASCULINE, FEMININE, NEUTRAL };
enum class DigitPronunciation { DIGIT_BY_DIGIT, FULL_NUMBER };

// ovos_number_parser.pronounce_number(number, lang, places, short_scale,
// scientific, ordinals, digits, gender, scale). An absent scale and
// short_scale give the language's own scale: short for a code that holds
// "br", long otherwise. An absent digits gives FULL_NUMBER. ValueError for NaN.
std::string pronounce_number(const Number& number, std::string_view lang, int places = 3,
                             std::optional<bool> short_scale = std::nullopt, bool scientific = false,
                             bool ordinals = false, std::optional<DigitPronunciation> digits = std::nullopt,
                             GrammaticalGender gender = GrammaticalGender::MASCULINE,
                             std::optional<Scale> scale = std::nullopt);

}  // namespace tugaphone::ovos_number_parser
