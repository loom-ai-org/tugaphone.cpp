#include "tugaphone/number_utils.hpp"

#include "common.hpp"
#include "modules.hpp"
#include "tugaphone/ovos_number_parser.hpp"

namespace parity {

namespace nu = tugaphone::number_utils;
namespace onp = tugaphone::ovos_number_parser;
using NP = nu::NumberParser;

namespace {

const Value& member(const Value& obj, const std::string& key) {
    for (const auto& [k, v] : obj.members) {
        if (k == key) return v;
    }
    throw std::runtime_error("no member " + key);
}

tugaphone::Number number_of(const Value& v) {
    if (v.members.at(0).first == "int") return tugaphone::BigInt::from_string(v.members.at(0).second.str);
    return *tugaphone::py_float(v.members.at(0).second.str);
}

std::string N(const tugaphone::Number& n) {
    if (const auto* i = std::get_if<tugaphone::BigInt>(&n)) return "{\"int\":" + J(i->str()) + "}";
    return "{\"float\":" + F(std::get<double>(n)) + "}";
}
std::string N(const std::optional<tugaphone::BigInt>& n) { return n ? N(tugaphone::Number(*n)) : "null"; }
std::string N(const std::optional<double>& n) { return n ? N(tugaphone::Number(*n)) : "null"; }

template <class E>
std::optional<E> enum_of(const Value& v, std::initializer_list<std::pair<const char*, E>> names) {
    if (v.is_null()) return std::nullopt;
    for (const auto& [name, value] : names) {
        if (v.str == name) return value;
    }
    throw std::runtime_error("unknown enum " + v.str);
}

std::optional<bool> opt_bool(const Value& v) {
    if (v.is_null()) return std::nullopt;
    return v.boolean;
}

std::string pronounce(const Value& v) {
    const tugaphone::Number n = number_of(v[1]);
    const std::string lang = v[2].str;
    const int places = static_cast<int>(v[3].integer);
    const auto short_scale = opt_bool(v[4]);
    const bool scientific = v[5].boolean, ordinals = v[6].boolean;
    const auto digits = enum_of<onp::DigitPronunciation>(
        v[7], {{"DIGIT_BY_DIGIT", onp::DigitPronunciation::DIGIT_BY_DIGIT},
               {"FULL_NUMBER", onp::DigitPronunciation::FULL_NUMBER}});
    const auto gender = enum_of<onp::GrammaticalGender>(v[8], {{"MASCULINE", onp::GrammaticalGender::MASCULINE},
                                                                {"FEMININE", onp::GrammaticalGender::FEMININE},
                                                                {"NEUTRAL", onp::GrammaticalGender::NEUTRAL}});
    const auto scale = enum_of<onp::Scale>(v[9], {{"SHORT", onp::Scale::SHORT}, {"LONG", onp::Scale::LONG}});
    std::string out = "[";
    for (size_t i = 0; i + 1 < v.size(); ++i) out += json_line::dump(v[i]) + ",";
    return out + guard([&] {
               return J(onp::pronounce_number(n, lang, places, short_scale, scientific, ordinals, digits, *gender,
                                              scale));
           }) + "]";
}

std::string token(const std::string& w) {
    std::vector<std::string> scientific;
    for (bool br : {false, true}) {
        for (const std::optional<std::string>& s :
             {std::optional<std::string>(), std::optional<std::string>("short"), std::optional<std::string>("long")}) {
            scientific.push_back(guard([&] { return J(NP::pronounce_scientific(w, br, s)); }));
        }
    }
    const auto [core, trail] = NP::split_trailing_punctuation(w);
    return "[\"token\"," + J(w) + "," +
           list({list({J(core), J(trail)}), guard([&] { return N(NP::to_int(w)); }),
                 guard([&] { return J(NP::is_int(w)); }), guard([&] { return N(NP::to_float(w)); }),
                 guard([&] { return J(NP::is_float(w)); }), guard([&] { return J(NP::is_decimal(w)); }),
                 guard([&] { return J(NP::is_scientific_notation(w)); }), list(scientific)}) +
           "]";
}

std::string sets() {
    auto set = [](const std::set<std::string>& s) { return J(s); };
    std::string out = "[\"sets\",{\"LABEL_HEADS\":" + set(NP::LABEL_HEADS()) +
                      ",\"PLURAL_FUNCTION_WORDS\":" + set(NP::PLURAL_FUNCTION_WORDS()) +
                      ",\"FEMININE_DETERMINERS\":" + set(NP::FEMININE_DETERMINERS()) +
                      ",\"MASCULINE_DETERMINERS\":" + set(NP::MASCULINE_DETERMINERS()) +
                      ",\"FUNCTION_WORDS\":" + set(NP::FUNCTION_WORDS()) + ",\"RANGE_LINKS\":" + set(NP::RANGE_LINKS()) +
                      "}," + J(std::string(NP::ORDINAL_MALE)) + "," + J(std::string(NP::ORDINAL_FEMALE)) + "," +
                      list({J(std::string(NP::ORDINAL_TOKENS[0])), J(std::string(NP::ORDINAL_TOKENS[1]))}) + "," +
                      J(std::string(NP::TRAILING_PUNCTUATION)) + ",";
    std::vector<std::string> units;
    for (const auto& u : NP::UNIT_NAMES()) units.push_back(list({J(u.symbol), list({J(u.singular), J(u.plural)})}));
    return out + list(units) + "]";
}

}  // namespace

std::string record_number_utils(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "pronounce_number") return pronounce(v);
    if (fn == "token") return token(v[1].str);
    if (fn == "sets") return sets();
    if (fn == "unit_name") {
        return head(v) + guard([&] { return J(NP::unit_name(v[1].str, v[2].boolean, v[3].boolean)); }) + "]";
    }
    if (fn == "unit_takes_plural") {
        std::optional<double> value;
        if (!v[1].is_null()) {
            const tugaphone::Number n = number_of(v[1]);
            value = std::holds_alternative<double>(n) ? std::get<double>(n)
                                                      : *tugaphone::py_float(std::get<tugaphone::BigInt>(n).str());
        }
        return head(v) + guard([&] { return J(NP::unit_takes_plural(value, v[2].boolean, v[3].boolean)); }) + "]";
    }
    if (fn == "context") {
        const std::string w = v[1].str;
        const auto prev = opt_str(v[2]), next = opt_str(v[3]);
        return "[" + J(fn) + "," + J(w) + "," + json_line::dump(v[2]) + "," + json_line::dump(v[3]) + "," +
               guard([&] { return J(NP::is_ordinal(w, next)); }) + "," +
               guard([&] { return J(NP::get_number_gender(w, prev, next)); }) + "]";
    }
    if (fn == "word") {
        return head(v) + guard([&] {
                   return J(NP::pronounce_number_word(v[1].str, opt_str(v[2]), opt_str(v[3]), opt_str(v[4]),
                                                      opt_bool(v[5]), v[6].boolean, opt_str(v[7])));
               }) + "]";
    }
    if (fn == "normalize_numbers") {
        return head(v) + guard([&] {
                   return J(nu::normalize_numbers(v[1].str, v[2].str, v[3].boolean, opt_str(v[4])));
               }) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
