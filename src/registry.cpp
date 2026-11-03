#include "tugaphone/registry.hpp"

#include <algorithm>
#include <map>

#include <orthography2ipa/orthography2ipa.hpp>

#include "detail.hpp"

namespace tugaphone::registry {

using detail::from_utf8;
using detail::to_utf8;
using detail::U32;

namespace {

const std::vector<std::string>& canonical() {
    static const std::vector<std::string> codes = [] {
        std::vector<std::string> out;
        for (const std::string& c : orthography2ipa::available_codes()) {
            for (std::string_view prefix : {"pt-", "pt_", "ext-PT", "roa-x-galaicopt"}) {
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

std::string resolve_lect(std::string_view lang) {
    std::string key = to_utf8(detail::lower(from_utf8(normalize_dialect_code(lang))));
    while (!key.empty()) {
        if (auto it = kAliases.find(key); it != kAliases.end()) return it->second;
        for (const std::string& code : canonical()) {
            if (to_utf8(detail::lower(from_utf8(code))) == key) return code;
        }
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
