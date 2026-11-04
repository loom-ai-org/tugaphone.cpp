// tugaphone (the package): TugaPhonemizer and the names the Python package
// re-exports at its top level. Including this header gives the whole public
// API of `import tugaphone`.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "tugaphone/accent.hpp"
#include "tugaphone/lattice_core.hpp"
#include "tugaphone/registry.hpp"
#include "tugaphone/version.hpp"

namespace tugaphone {

// The re-exports of tugaphone/__init__.py. __version__ is
// tugaphone::version::version, since the name version is the namespace.
using accent::AccentOverlay;
using accent::default_respell_rules;  // DEFAULT_RESPELL_RULES
using accent::force_accent;
using accent::respell;
using accent::respell_word;
using accent::RespellRule;
using accent::Transform;
using lattice_core::phonemize;
using registry::list_dialects;
using registry::resolve_lect;

// Dialect-aware Portuguese phonemization on the orthography2ipa lattice. Every
// Portuguese-family lect that list_dialects() names is reachable by its
// BCP-47 code.
class TugaPhonemizer {
public:
    // Space-separated IPA for each word of `sentence`, for the `lang` lect.
    // `contact` is "none" (the engine-only path), "auto", "es", "fr" or "en";
    // any other value raises ValueError.
    std::string phonemize_sentence(std::string_view sentence, std::string_view lang = "pt-PT",
                                   std::string_view contact = "none") const;

    // The Python regional_dialect= argument. It is ignored: the lect alone
    // selects the accent. Python issues a DeprecationWarning at run time; the
    // C++ compiler issues the deprecation warning at the call site.
    [[deprecated("regional_dialect is ignored; select the accent through lang (e.g. \"pt-PT-x-porto\")")]]
    std::string phonemize_sentence(std::string_view sentence, std::string_view lang, std::string_view contact,
                                   const std::optional<std::string>& regional_dialect) const;

    // accent::force_accent(text, lect, mode, base_lect, overlay).
    static std::string force_accent(std::string_view text, std::string_view lect, std::string_view mode = "ipa",
                                    std::string_view base_lect = "pt-PT", const AccentOverlay* overlay = nullptr);

    // Whether `lang` resolves to a known Portuguese lect. resolve_lect falls
    // back to pt-PT, so this is true for every string.
    static bool is_supported(std::string_view lang);
};

}  // namespace tugaphone
