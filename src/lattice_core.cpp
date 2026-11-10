#include "tugaphone/lattice_core.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <bifonia/bifonia.hpp>
#include <orthography2ipa/orthography2ipa.hpp>

#include "detail.hpp"
#include "o2i_registry.hpp"
#include "tugaphone/number_utils.hpp"
#include "tugaphone/registry.hpp"
#include "tugaphone/text_normalization.hpp"

namespace tugaphone::lattice_core {

namespace fs = std::filesystem;
namespace o2i = orthography2ipa;
using detail::s8;
using detail::u;
using detail::U32;

namespace {

std::map<std::string, std::unique_ptr<o2i::G2P>>& engines() {
    static std::map<std::string, std::unique_ptr<o2i::G2P>> map;
    return map;
}

// o2i_lexicons._tsv: the one .tsv file of a data directory.
std::map<std::string, std::string> read_one(const std::string& dir, const std::string& lect, const std::string& noun) {
    std::vector<fs::path> sources;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(detail::data_path(dir), ec)) {
        if (entry.path().extension() == ".tsv") sources.push_back(entry.path());
    }
    if (sources.size() != 1) {
        throw Error("LookupError", "no installed package supplies a " + noun + " file for '" + lect + "'");
    }
    return o2i::load_lexicon(sources[0].string());
}

// orthography2ipa.packaged_lexicon(lect, patches=True). The data directory
// holds the o2i-lexicons package data: the exceptions in <lect>/<source>.tsv
// and the patches in patches/<lect>/<source>.tsv. An exception wins over a
// patch for the same word.
std::map<std::string, std::string> packaged_lexicon(const std::string& lect) {
    auto lexicon = read_one("o2i-lexicons/" + lect, lect, "lexicon");
    auto patches = read_one("o2i-lexicons/patches/" + lect, lect, "patch");
    lexicon.merge(patches);
    return lexicon;
}

// The caller holds detail::o2i_registry_mutex().
const o2i::G2P& engine_locked(const std::string& lect) {
    auto& slot = engines()[lect];
    if (!slot) {
        try {
            const auto& packaged = registry::PACKAGED_LEXICON_LECTS();
            const o2i::Pipeline pipeline{
                packaged.count(lect) ? packaged_lexicon(lect) : std::map<std::string, std::string>{},
                nullptr,
                [lect](const std::string& text) { return lattice_core::expand(text, lect); },
                [lect](const std::string& text) { return bifonia::add_extra_diacritics(text, lect); }};
            slot = std::make_unique<o2i::G2P>(lect, std::map<std::string, std::vector<std::string>>{}, "", false,
                                              true, true, true, nullptr, nullptr, pipeline);
        } catch (...) {
            engines().erase(lect);
            throw;
        }
    }
    return *slot;
}

std::string transcribe(const std::string& lect, const std::string& text) {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    return engine_locked(lect).transcribe(text);
}

}  // namespace

const std::vector<std::string>& VALID_CONTACT() {
    static const std::vector<std::string> contacts = {"auto", "es", "fr", "en", "none"};
    return contacts;
}

std::string expand(std::string_view text, const std::string& lect) {
    return number_utils::normalize_numbers(text_normalization::normalize_orthography(text, lect), lect);
}

const o2i::G2P& engine(const std::string& lect) {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    return engine_locked(lect);
}

void validate_contact(std::string_view contact) {
    for (const auto& valid : VALID_CONTACT()) {
        if (valid == contact) return;
    }
    throw ValueError("unknown contact '" + std::string(contact) +
                     "'; expected one of ('auto', 'es', 'fr', 'en', 'none')");
}

std::string phonemize_runs(const std::string& lect, const std::vector<Run>& runs,
                           const ContactTranscriber& transcribe_contact) {
    std::vector<std::string> out;
    std::vector<U32> pt_buffer;
    auto flush = [&] {
        if (!pt_buffer.empty()) {
            out.push_back(transcribe(lect, s8(detail::join(pt_buffer, U" "))));
            pt_buffer.clear();
        }
    };
    for (const auto& [contact_lang, token] : runs) {
        if (contact_lang) {
            flush();
            out.push_back(transcribe_contact(token, *contact_lang));
        } else {
            pt_buffer.push_back(u(token));
        }
    }
    flush();
    std::string joined;
    bool first = true;
    for (const auto& part : out) {
        if (part.empty()) continue;
        if (!first) joined += " ";
        joined += part;
        first = false;
    }
    return joined;
}

std::string phonemize(std::string_view text, std::string_view lang, std::string_view contact) {
    validate_contact(contact);
    const std::string lect = detail::resolve_lect_from(lang, "phonemize");
    if (contact == "none") return transcribe(lect, std::string(text));
    const std::string side = registry::default_contact(lect);
    return phonemize_runs(lect, codeswitch::split_runs(text, contact, side),
                          [](const std::string& token, const std::string& lang) {
                              return codeswitch::transcribe_contact(token, lang);
                          });
}

void clear_caches() {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    engines().clear();
}

}  // namespace tugaphone::lattice_core
