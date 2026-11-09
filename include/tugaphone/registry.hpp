// tugaphone.registry: resolve a language code to the orthography2ipa lect
// that answers for it. A tugaphone dialect is an orthography2ipa lect code.
#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "tugaphone/dialects.hpp"

namespace tugaphone::registry {

// "PT-pt-X-PORTO" -> "pt-PT-x-porto". The tag structure is not validated.
std::string normalize_dialect_code(std::string_view lang);
// The lects that take a packaged lexicon: pt-PT and pt-BR. A sub-lect does not,
// because the lexicon would overwrite its spec with the parent's forms.
const std::set<std::string>& PACKAGED_LEXICON_LECTS();
// The release that removes lexicon_region.
extern const std::string LEXICON_REGION_REMOVAL_VERSION;

// The exact code, then a legacy alias, then the code with its trailing
// subtags removed one at a time; "pt-PT" when nothing resolves. A code that
// does not name a lect itself warns, because the caller gets another variety.
std::string resolve_lect(std::string_view lang = "pt-PT");
// Receives the text of each warning of this module, where Python calls
// warnings.warn. With no handler, the default writes "UserWarning: <text>" to
// stderr, once for each text and call site, as the default Python filter does.
// An empty handler restores the default. A handler runs under a lock, so it
// must not resolve a lect.
using WarningHandler = std::function<void(const std::string& message)>;
void set_warning_handler(WarningHandler handler);
// Deprecated: no lect takes a tugalex region map, so this is nullopt for every
// lect. Python emits a DeprecationWarning; the C++ compiler warns at the call.
[[deprecated("lexicon_region() is deprecated and returns None: no lect takes a tugalex region map. "
             "Use PACKAGED_LEXICON_LECTS. It will be removed in tugaphone 2.0.0.")]]
std::optional<std::string> lexicon_region(std::string_view lect);
// Every Portuguese orthography2ipa lect code in the modern orthography, sorted.
std::vector<std::string> list_dialects();
// "es" for a lect in contact with Spanish (pt-UY), "en" otherwise.
std::string default_contact(std::string_view lect);
// The national DialectInventory nearest to the resolved lect.
dialects::DialectInventory get_dialect_inventory(std::string_view lang = "pt-PT");
// Deprecated alias for resolve_lect. Python also emits a DeprecationWarning.
std::string resolve_dialect(std::string_view lang = "pt-PT");

}  // namespace tugaphone::registry
