// tugaphone.lattice_core: phonemization on the orthography2ipa lattice. A
// lect's engine is an orthography2ipa::G2P with tugaphone's layers:
// the normalizer (text_normalization, number_utils and bifonia homograph
// marks), the residual lexicon of o2i-lexicons for pt-PT and pt-BR, and the
// bundled splitter of orthography2ipa. The lexicon belongs to the engine, so a
// plain orthography2ipa::G2P in the same process stays on the rules.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tugaphone/codeswitch.hpp"

namespace orthography2ipa {
class G2P;
}

namespace tugaphone::lattice_core {

// _VALID_CONTACT.
const std::vector<std::string>& VALID_CONTACT();

// _normalizer(lect)(text): the text the lattice of `lect` reads.
std::string normalize(std::string_view text, const std::string& lect);

// engine(lect): the cached engine of an orthography2ipa lect code. The
// normalizer reaches the engine as a plugin override, which transcribe(),
// features() and sentence_lattice() all apply. The lexicon of a lect in
// registry::PACKAGED_LEXICON_LECTS comes from o2i-lexicons/<lect>/ in the data
// directory, and a missing file raises LookupError. Any other lect gets an
// empty lexicon, so only the rules answer it.
// orthography2ipa.cpp has no lock of its own: phonemize() holds a lock while
// it transcribes, but a caller that transcribes with this engine must not do
// so while another thread calls into tugaphone.
const orthography2ipa::G2P& engine(const std::string& lect);

// _validate_contact(contact): ValueError for a contact outside VALID_CONTACT.
void validate_contact(std::string_view contact);

// One run of split_runs: the contact language (nullopt for Portuguese) and
// the token.
using Run = codeswitch::Run;
using ContactTranscriber = std::function<std::string(const std::string& token, const std::string& lang)>;

// The loop of phonemize() for a contact mode: contiguous Portuguese tokens are
// one phrase for `lect`, each contact token goes to `transcribe_contact`, and
// the non-empty parts are joined by spaces.
std::string phonemize_runs(const std::string& lect, const std::vector<Run>& runs,
                           const ContactTranscriber& transcribe_contact);

// phonemize(text, lang, contact). contact "none" is the engine-only path; the
// other modes split the text with codeswitch::split_runs and transcribe each
// contact token with codeswitch::transcribe_contact.
std::string phonemize(std::string_view text, std::string_view lang = "pt-PT", std::string_view contact = "none");

// clear_caches(): drop the engines.
void clear_caches();

}  // namespace tugaphone::lattice_core
