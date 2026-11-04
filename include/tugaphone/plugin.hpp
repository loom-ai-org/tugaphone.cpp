// tugaphone.plugin: tugaphone on the orthography2ipa base interfaces.
//
// TugaphoneG2PPlugin is the full phonemizer behind the shared engine
// interface. SilabificadorSyllabifier is the Portuguese syllabifier as an
// orthography2ipa SyllabifierPlugin. Python registers plugins through package
// entry points; C++ has no such discovery, so a caller that wants either
// object hands it to orthography2ipa itself.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "orthography2ipa/orthography2ipa.hpp"
#include "tugaphone/accent.hpp"

namespace tugaphone::plugin {

// Dialect-aware Portuguese G2P through the tugaphone pipeline.
class TugaphoneG2PPlugin {
public:
    explicit TugaphoneG2PPlugin(std::string lang = "pt-PT", std::string contact = "none");

    std::string lang;
    std::string contact;

    // registry::list_dialects().
    std::vector<std::string> language_codes() const;
    // TugaPhonemizer().phonemize_sentence(text, lang, contact).
    std::string transcribe(std::string_view text) const;
    // As transcribe, with the lect of `context` when it names one.
    std::string transcribe_word(std::string_view word,
                                const orthography2ipa::sentence::WordContext* context = nullptr) const;
    // accent::force_accent; an unset or empty base_lect is the plugin's lang.
    std::string force_accent(std::string_view text, std::string_view lect, std::string_view mode = "ipa",
                             const std::optional<std::string>& base_lect = std::nullopt,
                             const accent::AccentOverlay* overlay = nullptr) const;
};

// Portuguese syllables from silabificador, for orthography2ipa's stress
// detection.
class SilabificadorSyllabifier : public orthography2ipa::SyllabifierPlugin {
public:
    // registry::list_dialects() and pt-CV, pt-GW, pt-MO, pt-ST, pt-GQ.
    std::vector<std::string> language_codes() const override;
    // silabificador::syllabify(word); `lang` is not read.
    std::vector<std::string> syllabify(const std::string& word, const std::string& lang = "") const override;
};

}  // namespace tugaphone::plugin
