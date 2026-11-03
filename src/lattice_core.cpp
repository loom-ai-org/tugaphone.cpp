#include "tugaphone/lattice_core.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>

#include <pwd.h>
#include <unistd.h>

#include <bifonia/bifonia.hpp>
#include <orthography2ipa/orthography2ipa.hpp>

#include "detail.hpp"
#include "tugaphone/dialects.hpp"
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

// The tugalex release the C++ lexicon port is held at parity with. The
// lattice_core goldens record the Python value, and the parity test checks it.
constexpr const char* kTugalexVersion = "2.2.2a1";

std::string home_dir() {
    if (const char* home = std::getenv("HOME")) return home;
    if (const passwd* pw = getpwuid(getuid())) return pw->pw_dir;
    return "~";
}

// os.path.join(a, b, ...): an empty part adds nothing but its separator.
std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

std::string default_cache_dir() {
    if (const char* dir = std::getenv("TUGAPHONE_LEXICON_DIR")) return dir;
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const std::string base = xdg ? std::string(xdg) : path_join(home_dir(), ".cache");
    return path_join(path_join(base, "tugaphone"), "lexicon");
}

std::string& cache_dir_slot() {
    static std::string dir = default_cache_dir();
    return dir;
}

std::set<std::string>& registered() {
    static std::set<std::string> set;
    return set;
}

std::map<std::string, std::unique_ptr<o2i::G2P>>& engines() {
    static std::map<std::string, std::unique_ptr<o2i::G2P>> map;
    return map;
}

// The orthography2ipa normalize stage of one lect: the Python G2P's
// normalizer= callable, registered as a plugin that answers for any code.
class Normalizer : public o2i::NormalizePlugin {
public:
    explicit Normalizer(std::string lect) : lect_(std::move(lect)) {}
    std::vector<std::string> language_codes() const override { return {"*"}; }
    std::string normalize(const std::string& text, const std::string& /*lang*/) const override {
        return lattice_core::normalize(text, lect_);
    }

private:
    std::string lect_;
};

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error("OSError", "cannot write " + path);
    out << text;
    if (!out) throw Error("OSError", "cannot write " + path);
}

}  // namespace

const std::vector<std::string>& VALID_CONTACT() {
    static const std::vector<std::string> contacts = {"auto", "es", "fr", "en", "none"};
    return contacts;
}

std::string relayout(std::string_view ipa) {
    static const U32 joiner = U"·";
    static const U32 marks[] = {U"ˈ", U"ˌ"};
    const U32 text = u(ipa);
    U32 out;
    size_t start = 0;
    while (true) {
        const size_t at = text.find(joiner, start);
        U32 syllable = text.substr(start, at == U32::npos ? U32::npos : at - start);
        for (const U32& mark : marks) {
            if (detail::contains(syllable, mark)) syllable = mark + detail::replace(syllable, mark, U"");
        }
        out += syllable;
        if (at == U32::npos) break;
        start = at + joiner.size();
    }
    return s8(out);
}

std::string tugalex_version() { return kTugalexVersion; }

const std::string& lexicon_cache_dir() { return cache_dir_slot(); }

void set_lexicon_cache_dir(std::string path) { cache_dir_slot() = std::move(path); }

std::string stamp_path(std::string_view path) {
    // pathlib's with_suffix: the suffix of the last component starts at its
    // last dot, unless that dot leads the name or ends it.
    const std::string p(path);
    const size_t slash = p.rfind('/');
    const size_t name_start = slash == std::string::npos ? 0 : slash + 1;
    const std::string name = p.substr(name_start);
    if (name.empty()) throw ValueError(p + " has an empty name");
    const size_t dot = name.rfind('.');
    const bool has_suffix = dot != std::string::npos && dot != 0 && dot + 1 != name.size();
    return p.substr(0, name_start) + (has_suffix ? name.substr(0, dot) : name) + ".version";
}

void write_lexicon(std::string_view region, const std::string& path) {
    const auto& ipa_map = dialects::LEXICON().get_ipa_map("NOUN", region);
    std::vector<const std::pair<std::string, std::string>*> entries;
    for (const auto& item : ipa_map) entries.push_back(&item);
    // sorted(ipa_map): UTF-8 byte order is code-point order.
    std::sort(entries.begin(), entries.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path());
    const std::string tmp = path + ".tmp";
    {
        std::string text;
        for (const auto* e : entries) text += e->first + "\t" + relayout(e->second) + "\n";
        write_text(tmp, text);
    }
    fs::rename(tmp, path);
    write_text(stamp_path(path), tugalex_version());
}

bool is_stale(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::file_size(path, ec) == 0) return true;
    std::ifstream in(stamp_path(path), std::ios::binary);
    if (!in) return true;
    const std::string stamp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s8(detail::strip_ws(u(stamp))) != tugalex_version();
}

void ensure_lexicon(const std::string& lect) {
    if (registered().count(lect)) return;
    const auto region = registry::lexicon_region(lect);
    if (!region) {
        registered().insert(lect);
        return;
    }
    const std::string dir = lexicon_cache_dir();
    const std::string path = dir.empty() ? lect + ".tsv" : path_join(dir, lect + ".tsv");
    if (is_stale(path)) write_lexicon(*region, path);
    o2i::register_lexicon(lect, path);
    registered().insert(lect);
}

std::string normalize(std::string_view text, const std::string& lect) {
    const std::string orthography = text_normalization::normalize_orthography(text, lect);
    return bifonia::add_extra_diacritics(number_utils::normalize_numbers(orthography, lect));
}

const o2i::G2P& engine(const std::string& lect) {
    auto& slot = engines()[lect];
    if (!slot) {
        try {
            ensure_lexicon(lect);
            const std::string plugin = "tugaphone:" + lect;
            o2i::register_normalize_plugin(plugin, std::make_shared<Normalizer>(lect));
            slot = std::make_unique<o2i::G2P>(lect, std::map<std::string, std::vector<std::string>>{
                                                        {"normalize", {plugin}}});
        } catch (...) {
            engines().erase(lect);
            throw;
        }
    }
    return *slot;
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
            out.push_back(engine(lect).transcribe(s8(detail::join(pt_buffer, U" "))));
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
    const std::string lect = registry::resolve_lect(lang);
    if (contact == "none") return engine(lect).transcribe(std::string(text));
    const std::string side = registry::default_contact(lect);
    return phonemize_runs(lect, codeswitch::split_runs(text, contact, side),
                          [](const std::string& token, const std::string& lang) {
                              return codeswitch::transcribe_contact(token, lang);
                          });
}

void clear_caches() {
    engines().clear();
    registered().clear();
}

}  // namespace tugaphone::lattice_core
