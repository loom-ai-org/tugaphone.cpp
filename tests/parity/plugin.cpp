#include "tugaphone/plugin.hpp"

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace tp = tugaphone::plugin;

std::string record_plugin(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "language_codes") return list({J(fn), J(tp::TugaphoneG2PPlugin().language_codes())});
    if (fn == "syllabifier_language_codes") return list({J(fn), J(tp::SilabificadorSyllabifier().language_codes())});
    if (fn == "syllabifier_priority") return list({J(fn), J(tp::SilabificadorSyllabifier().priority())});
    if (fn == "defaults") {
        const tp::TugaphoneG2PPlugin plugin;
        return list({J(fn), list({J(plugin.lang), J(plugin.contact)})});
    }
    if (fn == "syllabify") {
        const tp::SilabificadorSyllabifier syl;
        return head(v) + guard([&] {
                   return J(v[2].is_null() ? syl.syllabify(v[1].str) : syl.syllabify(v[1].str, v[2].str));
               }) + "]";
    }
    if (fn == "transcribe") {
        return head(v) + guard([&] { return J(tp::TugaphoneG2PPlugin(v[1].str, v[2].str).transcribe(v[3].str)); }) +
               "]";
    }
    if (fn == "transcribe_word") {
        return head(v) + guard([&] {
                   orthography2ipa::sentence::WordContext context;
                   context.lang = opt_str(v[5]);
                   const tp::TugaphoneG2PPlugin plugin(v[1].str, v[2].str);
                   return J(plugin.transcribe_word(v[3].str, v[4].boolean ? &context : nullptr));
               }) + "]";
    }
    if (fn == "force_accent") {
        return head(v) + guard([&] {
                   std::optional<tugaphone::accent::AccentOverlay> overlay;
                   if (!v[6].is_null()) overlay = tugaphone::accent::AccentOverlay::from_json(v[6].str);
                   const tp::TugaphoneG2PPlugin plugin(v[1].str);
                   return J(plugin.force_accent(v[2].str, v[3].str, v[4].str, opt_str(v[5]),
                                                overlay ? &*overlay : nullptr));
               }) + "]";
    }
    return "unknown record " + fn;
}

}  // namespace parity
