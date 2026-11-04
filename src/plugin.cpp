#include "tugaphone/plugin.hpp"

#include <utility>

#include "silabificador/silabificador.hpp"
#include "tugaphone/registry.hpp"
#include "tugaphone/tugaphone.hpp"

namespace tugaphone::plugin {

TugaphoneG2PPlugin::TugaphoneG2PPlugin(std::string lang_, std::string contact_)
    : lang(std::move(lang_)), contact(std::move(contact_)) {}

std::vector<std::string> TugaphoneG2PPlugin::language_codes() const { return registry::list_dialects(); }

std::string TugaphoneG2PPlugin::transcribe(std::string_view text) const {
    return TugaPhonemizer().phonemize_sentence(text, lang, contact);
}

std::string TugaphoneG2PPlugin::transcribe_word(std::string_view word,
                                                const orthography2ipa::sentence::WordContext* context) const {
    const std::string& target = context != nullptr && context->lang && !context->lang->empty() ? *context->lang : lang;
    return TugaPhonemizer().phonemize_sentence(word, target, contact);
}

std::string TugaphoneG2PPlugin::force_accent(std::string_view text, std::string_view lect, std::string_view mode,
                                             const std::optional<std::string>& base_lect,
                                             const accent::AccentOverlay* overlay) const {
    const std::string& base = base_lect && !base_lect->empty() ? *base_lect : lang;
    return accent::force_accent(text, lect, mode, base, overlay);
}

std::vector<std::string> SilabificadorSyllabifier::language_codes() const {
    std::vector<std::string> codes = registry::list_dialects();
    for (const char* code : {"pt-CV", "pt-GW", "pt-MO", "pt-ST", "pt-GQ"}) codes.emplace_back(code);
    return codes;
}

std::vector<std::string> SilabificadorSyllabifier::syllabify(const std::string& word,
                                                             const std::string& /*lang*/) const {
    return silabificador::syllabify(word);
}

}  // namespace tugaphone::plugin
