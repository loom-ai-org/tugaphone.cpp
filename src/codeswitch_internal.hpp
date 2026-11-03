// Internal pieces of tugaphone.codeswitch that the parity tests call directly.
// Each function that consults the Markov detector takes it as an argument;
// nullptr selects the orthographic fallback, as Python does when
// get_detector() returns None.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "tugaphone/codeswitch.hpp"
#include "tugaphone/langdetect.hpp"

namespace tugaphone::codeswitch::internal {

// _loanwords: the curated English loanword lexicon.
const std::set<std::string>& loanwords();

// _strip: lower case, without the surrounding characters that are neither
// alphabetic nor '-'.
std::string strip_token(std::string_view token);
// _has_english_signal on a stripped token.
bool has_english_signal(std::string_view core);
// _nativize: project contact IPA onto the Portuguese inventory.
std::string nativize(std::string_view ipa, bool english = false);
// _classify and _is_contact.
std::optional<std::string> classify(std::string_view token, std::string_view default_side,
                                    const langdetect::MarkovLangDetector* detector);
bool is_contact(std::string_view token, const langdetect::MarkovLangDetector* detector);
// split_runs with an explicit detector.
std::vector<Run> split_runs(std::string_view text, std::string_view contact, std::string_view default_side,
                            const langdetect::MarkovLangDetector* detector);

}  // namespace tugaphone::codeswitch::internal
