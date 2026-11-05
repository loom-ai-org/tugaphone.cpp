#include "tugaphone/lattice_core.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <pwd.h>
#include <unistd.h>

#include <bifonia/bifonia.hpp>
#include <orthography2ipa/orthography2ipa.hpp>
#include <tugalex/tugalex.hpp>

#include "detail.hpp"
#include "o2i_registry.hpp"
#include "pyio.hpp"
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

// os.path.expanduser("~/.cache").
std::string expand_cache_home() {
    std::string home;
    if (const char* env = std::getenv("HOME")) {
        home = env;
    } else if (const passwd* pw = getpwuid(getuid())) {
        home = pw->pw_dir;
    } else {
        return "~/.cache";
    }
    while (!home.empty() && home.back() == '/') home.pop_back();
    return home + "/.cache";
}

// os.path.join(a, b): an empty head adds nothing, not even a separator.
std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

// str(PurePosixPath(path)): empty and "." parts dropped, one or three or more
// leading slashes made one, exactly two kept, and "" made ".".
std::string pure_path(std::string_view path) {
    std::string root;
    size_t lead = 0;
    while (lead < path.size() && path[lead] == '/') ++lead;
    if (lead == 2) {
        root = "//";
    } else if (lead > 0) {
        root = "/";
    }
    std::string out = root;
    size_t start = lead;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view part = path.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (out.size() > root.size()) out += '/';
            out += part;
        }
        start = end + 1;
    }
    return out.empty() ? "." : out;
}

// Path(dir) / name for a pure path `dir`.
std::string path_div(const std::string& dir, const std::string& name) {
    return dir == "." ? name : path_join(dir, name);
}

// PurePosixPath.with_suffix(suffix), after pure_path: the suffix of the name
// starts at its last dot, unless that dot leads the name or ends it.
std::string with_suffix(std::string_view path, const std::string& suffix) {
    const std::string p = pure_path(path);
    const size_t slash = p.rfind('/');
    const size_t name_start = slash == std::string::npos ? 0 : slash + 1;
    const std::string name = p.substr(name_start);
    if (name.empty() || p == ".") throw ValueError(p + " has an empty name");
    const size_t dot = name.rfind('.');
    const bool has_suffix = dot != std::string::npos && dot != 0 && dot + 1 != name.size();
    return p.substr(0, name_start) + (has_suffix ? name.substr(0, dot) : name) + suffix;
}

std::string default_lexicon_cache_dir() {
    if (const char* dir = std::getenv("TUGAPHONE_LEXICON_DIR")) return pure_path(dir);
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const std::string base = xdg ? std::string(xdg) : expand_cache_home();
    return pure_path(path_join(path_join(base, "tugaphone"), "lexicon"));
}

std::string& cache_dir_slot() {
    static std::string dir = default_lexicon_cache_dir();
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

std::string tugalex_version() { return tugalex::kPythonReference; }

const std::string& lexicon_cache_dir() { return cache_dir_slot(); }

void set_lexicon_cache_dir(std::string path) { cache_dir_slot() = pure_path(path); }

std::string stamp_path(std::string_view path) { return with_suffix(path, ".version"); }

void write_lexicon(std::string_view region, const std::string& path) {
    const tugalex::OrderedMap<std::string>* ipa_map = nullptr;
    try {
        ipa_map = &dialects::LEXICON().get_ipa_map("NOUN", region);
    } catch (const std::invalid_argument& e) {
        throw ValueError(e.what());
    }
    std::vector<const std::pair<std::string, std::string>*> entries;
    for (const auto& item : *ipa_map) entries.push_back(&item);
    // sorted(ipa_map): UTF-8 byte order is code-point order.
    std::sort(entries.begin(), entries.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
    // pathlib: path.parent.mkdir(parents=True, exist_ok=True), then a
    // ".tsv.tmp" sibling replaced onto the path. Errors name pathlib's
    // normalised paths.
    const std::string target = detail::pyio::pure_path(path);
    const size_t slash = target.rfind('/');
    detail::pyio::mkdir_parents(slash == std::string::npos ? "." : slash == 0 ? "/" : target.substr(0, slash));
    const std::string tmp = with_suffix(target, ".tsv.tmp");
    {
        std::string text;
        for (const auto* e : entries) text += e->first + "\t" + relayout(e->second) + "\n";
        detail::pyio::write_text(tmp, text);
    }
    detail::pyio::replace(tmp, target);
    detail::pyio::write_text(stamp_path(target), tugalex_version());
}

bool is_stale(const std::string& path) {
    const std::string target = detail::pyio::pure_path(path);
    if (!detail::pyio::path_exists(target)) return true;
    std::error_code ec;
    if (fs::file_size(target, ec) == 0) return true;
    std::string stamp;
    try {
        stamp = detail::pyio::read_text(stamp_path(target));
    } catch (const Error&) {
        // except (OSError, UnicodeDecodeError): a stamp that cannot be read or
        // is not UTF-8 is stale.
        return true;
    }
    return s8(detail::strip_ws(u(stamp))) != tugalex_version();
}

namespace {

// The caller holds detail::o2i_registry_mutex().
void ensure_lexicon_locked(const std::string& lect) {
    if (registered().count(lect)) return;
    const auto region = registry::lexicon_region(lect);
    if (!region) {
        registered().insert(lect);
        return;
    }
    const std::string path = path_div(lexicon_cache_dir(), lect + ".tsv");
    if (is_stale(path)) write_lexicon(*region, path);
    o2i::register_lexicon(lect, path);
    registered().insert(lect);
}

// The caller holds detail::o2i_registry_mutex().
const o2i::G2P& engine_locked(const std::string& lect) {
    auto& slot = engines()[lect];
    if (!slot) {
        try {
            ensure_lexicon_locked(lect);
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

std::string transcribe(const std::string& lect, const std::string& text) {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    return engine_locked(lect).transcribe(text);
}

}  // namespace

void ensure_lexicon(const std::string& lect) {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    ensure_lexicon_locked(lect);
}

std::string normalize(std::string_view text, const std::string& lect) {
    const std::string orthography = text_normalization::normalize_orthography(text, lect);
    return bifonia::add_extra_diacritics(number_utils::normalize_numbers(orthography, lect), lect);
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
    const std::string lect = registry::resolve_lect(lang);
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
    registered().clear();
}

}  // namespace tugaphone::lattice_core

std::string tugaphone::detail::default_lexicon_cache_dir() {
    return tugaphone::lattice_core::default_lexicon_cache_dir();
}
