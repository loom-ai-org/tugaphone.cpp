// tugaphone.codeswitch: embedded Spanish, French and English words, detected
// per token, transcribed through the orthography2ipa contact lattice and
// projected onto the Portuguese phoneme inventory.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tugaphone::codeswitch {

// Whether `token` is a whole-word, case-insensitive hit in the curated
// English loanword lexicon (data/loanwords.json).
bool is_known_loanword(std::string_view token);
// The orthographic heuristic: whether `token` is contact-language text.
bool is_contact_word(std::string_view token);
// "en", "fr" or "es" for a contact token, "pt" for a Portuguese keep-list
// wordform, and `default_side` when no signal decides.
std::string contact_language(std::string_view token, std::string_view default_side = "en");
// Transcribe `word` with the es-ES, fr-FR or en-US lattice ("es", "fr" or
// "en") and project the IPA onto the Portuguese inventory. KeyError for any
// other contact code.
std::string transcribe_contact(std::string_view word, std::string_view contact);

// One (contact language, token) pair; the language is nullopt for Portuguese.
using Run = std::pair<std::optional<std::string>, std::string>;
// Split `text` on whitespace and route each token: contact "none" keeps every
// token Portuguese, "auto" classifies each one, and "es", "fr" or "en" routes
// every detected contact token through that language. An abbreviation that
// only Portuguese reads in its context stays Portuguese in every mode
// (text_normalization::stays_portuguese): "Dr.", "Av.", "Cap." and its number.
std::vector<Run> split_runs(std::string_view text, std::string_view contact, std::string_view default_side = "en");

}  // namespace tugaphone::codeswitch
