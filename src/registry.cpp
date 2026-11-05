#include "tugaphone/registry.hpp"

#include <algorithm>
#include <map>
#include <mutex>

#include <orthography2ipa/orthography2ipa.hpp>

#include "detail.hpp"
#include "o2i_registry.hpp"

namespace tugaphone::registry {

using detail::from_utf8;
using detail::to_utf8;
using detail::U32;

namespace {

const std::vector<std::string>& canonical() {
    static const std::vector<std::string> codes = [] {
        // The Galician-Portuguese key goes through resolve, because
        // orthography2ipa keeps a renamed spec's old key as an exact alias.
        std::string galician_portuguese;
        std::vector<std::string> available;
        {
            const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
            galician_portuguese = orthography2ipa::resolve("roa-x-galaicopt");
            available = orthography2ipa::available_codes();
        }
        std::vector<std::string> out;
        for (const std::string& c : available) {
            for (std::string_view prefix : {std::string_view("pt-"), std::string_view("pt_"),
                                            std::string_view("ext-PT"), std::string_view(galician_portuguese)}) {
                if (c.compare(0, prefix.size(), prefix) == 0) {
                    out.push_back(c);
                    break;
                }
            }
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

const std::map<std::string, std::string> kLexiconRegion = {
    {"pt-PT", "lbx"}, {"pt-PT-x-lisbon", "lbx"}, {"pt-BR", "rjx"}, {"pt-BR-x-rj", "rjx"},
    {"pt-BR-x-sp", "spx"}, {"pt-AO", "lda"}, {"pt-MZ", "mpx"}, {"pt-TL", "dli"},
};

}  // namespace

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

std::string resolve_lect(std::string_view lang) {
    std::string key = to_utf8(detail::lower(from_utf8(normalize_dialect_code(lang))));
    while (!key.empty()) {
        const auto alias = kAliases.find(key);
        if (auto lect = registered(alias == kAliases.end() ? key : alias->second)) return *lect;
        const size_t dash = key.rfind('-');
        if (dash == std::string::npos) break;
        key.resize(dash);
    }
    return "pt-PT";
}

std::optional<std::string> lexicon_region(std::string_view lect) {
    auto it = kLexiconRegion.find(std::string(lect));
    if (it == kLexiconRegion.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> list_dialects() { return canonical(); }

std::string default_contact(std::string_view lect) { return lect == "pt-UY" ? "es" : "en"; }

dialects::DialectInventory get_dialect_inventory(std::string_view lang) {
    const std::string lect = resolve_lect(lang);
    if (lect.rfind("pt-BR", 0) == 0) return dialects::BrazilianPortuguese();
    if (lect == "pt-AO") return dialects::AngolanPortuguese();
    if (lect == "pt-MZ") return dialects::MozambicanPortuguese();
    if (lect == "pt-TL") return dialects::TimoresePortuguese();
    return dialects::EuropeanPortuguese();
}

std::string resolve_dialect(std::string_view lang) { return resolve_lect(lang); }

}  // namespace tugaphone::registry
