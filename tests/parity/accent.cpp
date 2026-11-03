#include "accent_internal.hpp"
#include "common.hpp"
#include "detail.hpp"
#include "modules.hpp"
#include "tugaphone/accent.hpp"

namespace parity {

namespace ac = tugaphone::accent;
namespace detail = tugaphone::detail;

namespace {

// fake_phonemize of tools/goldens/accent.py, code point for code point.
std::string fake_phonemize(const std::string& text, const std::string& lect) {
    using detail::U32;
    U32 t = detail::lower(detail::u(text));
    const U32 l = detail::u(lect);
    const bool north = detail::contains(l, U"porto") || detail::contains(l, U"braga") || detail::contains(l, U"minho");
    const bool br = !north && detail::starts_with(l, U"pt-BR");
    const bool africa = !north && !br && (detail::starts_with(l, U"pt-AO") || detail::starts_with(l, U"pt-MZ"));
    if (north) t = detail::replace(t, U"v", U"b");
    if (br) {
        t = detail::replace(t, U"ti", U"tʃi");
        t = detail::replace(t, U"di", U"dʒi");
        t = detail::replace(t, U"ei", U"e");
        t = detail::replace(t, U"ou", U"o");
        if (detail::ends_with(t, U"te")) t = t.substr(0, t.size() - 2) + U"tʃi";
        if (detail::ends_with(t, U"de")) t = t.substr(0, t.size() - 2) + U"dʒi";
        if (detail::ends_with(t, U"l")) t = t.substr(0, t.size() - 1) + U"w";
        t = detail::replace(t, U"l ", U"w ");
    }
    if (africa) t = detail::replace(t, U"ou", U"o");
    const std::pair<const char32_t*, const char32_t*> base[] = {
        {U"tch", U"tʃ"}, {U"dj", U"dʒ"}, {U"ch", U"ʃ"}, {U"lh", U"ʎ"}, {U"nh", U"ɲ"},
        {U"ç", U"s"},    {U"ê", U"e"},   {U"ô", U"o"},  {U"ei", U"ɐj"}, {U"ou", U"ow"},
    };
    for (const auto& [a, b] : base) t = detail::replace(t, a, b);
    return detail::s8(t);
}

const ac::Phonemizer kFake = fake_phonemize;

std::string spans(const std::vector<std::pair<size_t, size_t>>& found) {
    std::vector<std::string> parts;
    for (const auto& [start, end] : found) parts.push_back(list({J(static_cast<long long>(start)), J(static_cast<long long>(end))}));
    return list(parts);
}

std::vector<ac::RespellRule> rules_of(const Value& v) {
    std::vector<ac::RespellRule> rules;
    for (const auto& r : v.items) rules.emplace_back(r[0].str, r[1].str, r[2].str);
    return rules;
}

std::string overlay_outputs(const ac::AccentOverlay& o) {
    return list({J(o.to_dict_json()), J(o.to_json()), J(o.to_json(std::nullopt)), J(o.to_json(2, true)),
                 J(o.to_json(0)), J(o.to_json(4))});
}

}  // namespace

std::string record_accent(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "default_rules") {
        std::vector<std::string> parts;
        for (const auto& r : ac::default_respell_rules()) {
            parts.push_back(list({J(r.name()), J(r.pattern()), J(r.replacement()), J(r.note())}));
        }
        return "[" + J(fn) + "," + list(parts) + "]";
    }
    if (fn == "rule_matches") {
        std::vector<std::string> parts;
        for (const auto& r : ac::default_respell_rules()) parts.push_back(spans(r.matches(v[1].str)));
        return head(v) + list(parts) + "]";
    }
    if (fn == "lev") {
        return head(v) + J(static_cast<long long>(ac::internal::levenshtein(detail::u(v[1].str), detail::u(v[2].str)))) + "]";
    }
    if (fn == "restore_case") {
        return head(v) + J(detail::s8(ac::internal::restore_case(detail::u(v[1].str), detail::u(v[2].str)))) + "]";
    }
    if (fn == "word_spans") return head(v) + spans(ac::internal::word_spans(detail::u(v[1].str))) + "]";
    if (fn == "respell_word") {
        return head(v) + J(ac::respell_word(v[1].str, v[2].str, v[3].str, ac::default_respell_rules(), kFake)) + "]";
    }
    if (fn == "respell") {
        return head(v) + J(ac::respell(v[1].str, v[2].str, v[3].str, ac::default_respell_rules(), kFake)) + "]";
    }
    if (fn == "respell_word_rules") {
        return head(v) + guard([&] {
                   return J(ac::respell_word(v[1].str, v[2].str, v[3].str, rules_of(v[4]), kFake));
               }) + "]";
    }
    if (fn == "transform") {
        ac::Transform t;
        t.kind = v[1].str;
        t.pattern = v[2].str;
        t.replacement = v[3].str;
        t.ignore_case = v[4].boolean;
        return head(v) + guard([&] { return J(t.apply(v[5].str)); }) + "]";
    }
    if (fn == "user_rule") {
        return head(v) + guard([&] { return spans(ac::RespellRule("u", v[1].str, "").matches(v[2].str)); }) + "]";
    }
    if (fn == "overlay_json") {
        return head(v) + guard([&] { return overlay_outputs(ac::AccentOverlay::from_json(v[1].str)); }) + "]";
    }
    if (fn == "transform_json") {
        return head(v) + guard([&] { return J(ac::Transform::from_dict_json(v[1].str).to_dict_json()); }) + "]";
    }
    if (fn == "overlay_apply") {
        return head(v) + guard([&] { return J(ac::AccentOverlay::from_json(v[1].str).apply(v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "force_accent") {
        return head(v) + guard([&] {
                   std::optional<ac::AccentOverlay> overlay;
                   if (!v[5].is_null()) overlay = ac::AccentOverlay::from_json(v[5].str);
                   return J(ac::force_accent(v[1].str, v[2].str, v[3].str, v[4].str, overlay ? &*overlay : nullptr,
                                             ac::default_respell_rules(), kFake));
               }) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
