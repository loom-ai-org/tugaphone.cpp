#include "tugaphone/tugaphone.hpp"

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace {

const char* const kModes[] = {"none", "en", "es", "fr", "auto"};

}  // namespace

std::string record_package(const Value& v) {
    const std::string& fn = v[0].str;
    const tugaphone::TugaPhonemizer ph;
    if (fn == "phonemize_sentence") {
        std::vector<std::string> outs;
        for (const char* mode : kModes)
            outs.push_back(guard([&] { return J(ph.phonemize_sentence(v[1].str, v[2].str, mode)); }));
        return head(v) + list(outs) + "]";
    }
    if (fn == "regional_dialect") {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        return head(v) + guard([&] {
                   return J(ph.phonemize_sentence(v[1].str, v[2].str, v[3].str, opt_str(v[4])));
               }) + "]";
#pragma GCC diagnostic pop
    }
    if (fn == "phonemize") {
        return head(v) + guard([&] { return J(tugaphone::phonemize(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "is_supported") return head(v) + J(tugaphone::TugaPhonemizer::is_supported(v[1].str)) + "]";
    if (fn == "resolve_lect") return head(v) + guard([&] { return J(tugaphone::resolve_lect(v[1].str)); }) + "]";
    if (fn == "force_accent") {
        return head(v) + guard([&] {
                   std::optional<tugaphone::AccentOverlay> overlay;
                   if (!v[5].is_null()) overlay = tugaphone::AccentOverlay::from_json(v[5].str);
                   return J(tugaphone::TugaPhonemizer::force_accent(v[1].str, v[2].str, v[3].str, v[4].str,
                                                                    overlay ? &*overlay : nullptr));
               }) + "]";
    }
    if (fn == "respell_word") {
        return head(v) + guard([&] { return J(tugaphone::respell_word(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "respell") return head(v) + guard([&] { return J(tugaphone::respell(v[1].str, v[2].str)); }) + "]";
    if (fn == "__version__") return list({J(fn), J(tugaphone::version::version)});
    if (fn == "list_dialects") return list({J(fn), J(tugaphone::list_dialects())});
    if (fn == "DEFAULT_RESPELL_RULES") {
        std::vector<std::string> rules;
        for (const auto& r : tugaphone::default_respell_rules())
            rules.push_back(list({J(r.name()), J(r.pattern()), J(r.replacement()), J(r.note())}));
        return list({J(fn), list(rules)});
    }
    return "unknown record " + fn;
}

}  // namespace parity
