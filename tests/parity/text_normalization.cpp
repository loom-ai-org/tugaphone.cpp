#include "tugaphone/text_normalization.hpp"

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace tn = tugaphone::text_normalization;

std::string record_text_normalization(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn != "text") return "unknown call " + fn;
    const std::string text = v[1].str, lang = v[2].str;
    return "[" + J(fn) + "," + J(text) + "," + J(lang) + "," +
           list({
               guard([&] { return J(tn::split_number_ranges(text)); }),
               guard([&] { return J(tn::normalize_number_separators(text)); }),
               guard([&] { return J(tn::separate_unit_symbols(text)); }),
               guard([&] { return J(tn::expand_clock_times(text)); }),
               guard([&] { return J(tn::expand_abbreviations(text)); }),
               guard([&] { return J(tn::expand_regnal_numerals(text, lang)); }),
               guard([&] { return J(tn::expand_acronyms(text)); }),
               guard([&] { return J(tn::expand_letter_digit_codes(text, lang)); }),
               guard([&] { return J(tn::normalize_orthography(text, lang)); }),
           }) +
           "]";
}

}  // namespace parity
