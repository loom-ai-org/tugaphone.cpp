#include "tugaphone/tugaphone.hpp"

#include "detail.hpp"

namespace tugaphone {

std::string TugaPhonemizer::phonemize_sentence(std::string_view sentence, std::string_view lang,
                                               std::string_view contact) const {
    return lattice_core::phonemize(sentence, lang, contact);
}

std::string TugaPhonemizer::phonemize_sentence(std::string_view sentence, std::string_view lang,
                                               std::string_view contact,
                                               const std::optional<std::string>& /*regional_dialect*/) const {
    return lattice_core::phonemize(sentence, lang, contact);
}

std::string TugaPhonemizer::force_accent(std::string_view text, std::string_view lect, std::string_view mode,
                                         std::string_view base_lect, const AccentOverlay* overlay) {
    return accent::force_accent(text, lect, mode, base_lect, overlay);
}

bool TugaPhonemizer::is_supported(std::string_view lang) {
    detail::resolve_lect_from(lang, "is_supported");
    return true;
}

}  // namespace tugaphone
