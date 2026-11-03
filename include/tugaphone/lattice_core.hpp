// tugaphone.lattice_core: phonemization on the orthography2ipa lattice. A
// lect's engine is an orthography2ipa::G2P with tugaphone's layers:
// the normalizer (text_normalization, number_utils and bifonia homograph
// marks), the tugalex lexicon registered for the lect, and the silabificador
// syllabifier that orthography2ipa.cpp links.
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

// _relayout(ipa): a tugalex entry ("gˈa·tʊ") in the spec layout ("ˈgatʊ").
std::string relayout(std::string_view ipa);

// _tugalex_version(): the tugalex release the lexicon cache is stamped with.
std::string tugalex_version();

// _LEXICON_CACHE: TUGAPHONE_LEXICON_DIR, else $XDG_CACHE_HOME/tugaphone/lexicon,
// else ~/.cache/tugaphone/lexicon, read once. set_lexicon_cache_dir replaces
// it, as a test replaces the Python module attribute.
const std::string& lexicon_cache_dir();
void set_lexicon_cache_dir(std::string path);

// _stamp_path(path): the path with its last suffix replaced by ".version".
std::string stamp_path(std::string_view path);
// _write_lexicon(region, path): the region's lexicon as a sorted TSV, then
// the version stamp.
void write_lexicon(std::string_view region, const std::string& path);
// _is_stale(path).
bool is_stale(const std::string& path);
// _ensure_lexicon(lect): register the lect's tugalex lexicon once.
void ensure_lexicon(const std::string& lect);
// _normalizer(lect)(text): the text the lattice of `lect` reads.
std::string normalize(std::string_view text, const std::string& lect);

// engine(lect): the cached engine of an orthography2ipa lect code. The
// normalizer reaches the engine as a plugin override. orthography2ipa.cpp
// reads the overrides in transcribe() and transcribe_detailed(); its
// features() and sentence_lattice() read them only from
// loom-ai-org/orthography2ipa.cpp#26 on, and skip the normalizer before it.
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

// clear_caches(): drop the engines and the lexicon registrations.
void clear_caches();

}  // namespace tugaphone::lattice_core
