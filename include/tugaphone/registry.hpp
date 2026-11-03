// tugaphone.registry: resolve a language code to the orthography2ipa lect
// that answers for it. A tugaphone dialect is an orthography2ipa lect code.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tugaphone/dialects.hpp"

namespace tugaphone::registry {

// "PT-pt-X-PORTO" -> "pt-PT-x-porto". The tag structure is not validated.
std::string normalize_dialect_code(std::string_view lang);
// The exact code, then a legacy alias, then the code with its trailing
// subtags removed one at a time; "pt-PT" when nothing resolves.
std::string resolve_lect(std::string_view lang = "pt-PT");
// The tugalex region whose lexicon overlays `lect`, or nullopt.
std::optional<std::string> lexicon_region(std::string_view lect);
// Every Portuguese-family orthography2ipa lect code, sorted.
std::vector<std::string> list_dialects();
// "es" for a lect in contact with Spanish (pt-UY), "en" otherwise.
std::string default_contact(std::string_view lect);
// The national DialectInventory nearest to the resolved lect.
dialects::DialectInventory get_dialect_inventory(std::string_view lang = "pt-PT");
// Deprecated alias for resolve_lect. Python also emits a DeprecationWarning.
std::string resolve_dialect(std::string_view lang = "pt-PT");

}  // namespace tugaphone::registry
