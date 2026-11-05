#include "tugaphone/text_normalization.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace tn = tugaphone::text_normalization;

std::string record_stays_portuguese(const Value& v) {
    const std::vector<std::string> words = v[1].strings();
    const auto i = static_cast<size_t>(v[2].integer);
    std::function<bool(const std::string&)> is_foreign;
    std::set<std::string> foreign;
    if (!v[3].is_null()) {
        const auto chosen = v[3].strings();
        foreign.insert(chosen.begin(), chosen.end());
        is_foreign = [&](const std::string& word) { return foreign.count(word) > 0; };
    }
    return head(v) + guard([&] { return J(tn::stays_portuguese(words, i, is_foreign)); }) + "]";
}

std::string record_text_normalization(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "stays_portuguese") return record_stays_portuguese(v);
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
