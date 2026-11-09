#include "tugaphone/registry.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <utility>

#include <orthography2ipa/orthography2ipa.hpp>

#include "detail.hpp"
#include "o2i_registry.hpp"
#include "pyio.hpp"
#include "tugaphone/version.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::registry {

using detail::from_utf8;
using detail::to_utf8;
using detail::U32;

namespace {

// _OWN_ORTHOGRAPHY: a lect written in another orthography, so the modern
// Portuguese text stages do not apply.
constexpr std::string_view kOwnOrthography = "pt-PT-x-medieval";

const std::vector<std::string>& canonical() {
    static const std::vector<std::string> codes = [] {
        std::vector<std::string> available;
        {
            const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
            available = orthography2ipa::available_codes();
        }
        std::vector<std::string> out;
        for (const std::string& c : available) {
            if ((c.rfind("pt-", 0) == 0 || c.rfind("pt_", 0) == 0) && c != kOwnOrthography) out.push_back(c);
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return codes;
}

const std::map<std::string, std::string> kAliases = {
    {"pt", "pt-PT"},
    {"pt-pt-x-lisboa", "pt-PT-x-lisbon"},
    {"pt-pt-x-acores", "pt-PT-x-acores"},
    {"pt-pt-x-azores", "pt-PT-x-acores"},
    {"pt-pt-x-sao-miguel", "pt-PT-x-sao-miguel"},
    {"pt-pt-x-north", "pt-PT-x-minho"},
    {"pt-pt-x-norte", "pt-PT-x-minho"},
    {"pt-pt-x-fafe", "pt-PT-x-minho"},
    {"pt-pt-x-famalicao", "pt-PT-x-viana"},
    {"pt-pt-x-transmontano", "pt-PT-x-trasosmontes"},
    {"pt-pt-x-tras-os-montes", "pt-PT-x-trasosmontes"},
    {"pt-pt-x-central", "pt-PT-x-coimbra"},
    {"pt-br-x-sao-paulo", "pt-BR-x-sp"},
    {"pt-br-x-rio-janeiro", "pt-BR-x-rj"},
    {"pt-br-x-rio", "pt-BR-x-rj"},
};

// The warning state. A handler runs while the mutex is held, so it must not
// call back into the warning functions.
struct Warnings {
    std::mutex mutex;
    WarningHandler handler;
    std::set<std::pair<std::string, std::string>> shown;
};

Warnings& warnings() {
    static Warnings state;
    return state;
}

// warnings.warn(message, stacklevel=2) from `call_site`. The default Python
// filter prints a text once for each location of the caller.
void warn(std::string_view call_site, const std::string& message) {
    Warnings& state = warnings();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (state.handler) {
        state.handler(message);
    } else if (state.shown.emplace(call_site, message).second) {
        std::cerr << "UserWarning: " << message << "\n";
    }
}

}  // namespace

const std::set<std::string>& PACKAGED_LEXICON_LECTS() {
    static const std::set<std::string> lects = {"pt-PT", "pt-BR"};
    return lects;
}

const std::string LEXICON_REGION_REMOVAL_VERSION = std::to_string(version::VERSION_MAJOR + 1) + ".0.0";

void set_warning_handler(WarningHandler handler) {
    Warnings& state = warnings();
    const std::lock_guard<std::mutex> lock(state.mutex);
    state.handler = std::move(handler);
}

std::string normalize_dialect_code(std::string_view lang) {
    const U32 text = detail::strip_ws(from_utf8(lang));
    std::vector<U32> segments;
    size_t start = 0;
    while (true) {
        const size_t dash = text.find(U'-', start);
        segments.push_back(text.substr(start, dash == U32::npos ? U32::npos : dash - start));
        if (dash == U32::npos) break;
        start = dash + 1;
    }
    std::vector<U32> normalized;
    bool in_private_use = false;
    for (size_t i = 0; i < segments.size(); ++i) {
        const U32 low = detail::lower(segments[i]);
        if (low == U"x") {
            in_private_use = true;
            normalized.push_back(low);
        } else if (i > 0 && segments[i].size() == 2 && !in_private_use) {
            normalized.push_back(detail::upper(segments[i]));
        } else {
            normalized.push_back(low);
        }
    }
    return to_utf8(detail::join(normalized, U"-"));
}

namespace {

// registry.py _registered: the canonical dialect that code names exactly. A
// code orthography2ipa renamed reaches its spec through the exact alias kept
// for the old key; a nearest-language match does not count.
std::optional<std::string> registered(const std::string& code) {
    // canonical() takes the mutex the first time, so it runs before the lock.
    const std::vector<std::string>& lects = canonical();
    const std::string folded = to_utf8(detail::lower(from_utf8(code)));
    for (const std::string& lect : lects) {
        if (to_utf8(detail::lower(from_utf8(lect))) == folded) return lect;
    }
    std::string lect;
    {
        const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
        if (!orthography2ipa::resolves_exactly(code)) return std::nullopt;
        lect = orthography2ipa::resolve(code);
    }
    if (std::binary_search(lects.begin(), lects.end(), lect)) return lect;
    return std::nullopt;
}

}  // namespace

std::string resolve_lect(std::string_view lang) { return detail::resolve_lect_from(lang, "resolve_lect"); }

std::optional<std::string> lexicon_region(std::string_view) { return std::nullopt; }

std::vector<std::string> list_dialects() { return canonical(); }

std::string default_contact(std::string_view lect) { return lect == "pt-UY" ? "es" : "en"; }

dialects::DialectInventory get_dialect_inventory(std::string_view lang) {
    const std::string lect = detail::resolve_lect_from(lang, "get_dialect_inventory");
    if (lect.rfind("pt-BR", 0) == 0) return dialects::BrazilianPortuguese();
    if (lect == "pt-AO") return dialects::AngolanPortuguese();
    if (lect == "pt-MZ") return dialects::MozambicanPortuguese();
    if (lect == "pt-TL") return dialects::TimoresePortuguese();
    return dialects::EuropeanPortuguese();
}

std::string resolve_dialect(std::string_view lang) { return detail::resolve_lect_from(lang, "resolve_dialect"); }

}  // namespace tugaphone::registry

std::string tugaphone::detail::resolve_lect_from(std::string_view lang, std::string_view call_site) {
    const std::string canon = to_utf8(lower(from_utf8(registry::normalize_dialect_code(lang))));
    std::string key = canon;
    std::optional<std::string> lect;
    while (!key.empty()) {
        const auto alias = registry::kAliases.find(key);
        lect = registry::registered(alias == registry::kAliases.end() ? key : alias->second);
        if (lect) break;
        const size_t dash = key.rfind('-');
        key.resize(dash == std::string::npos ? 0 : dash);
    }
    const std::string resolved = lect.value_or("pt-PT");
    if (key != canon || !lect) {
        registry::warn(call_site, py_str_repr(pyio::fsdecode(lang)) + " is not a tugaphone lect; using " + resolved);
    }
    return resolved;
}
