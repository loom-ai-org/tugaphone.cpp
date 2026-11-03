#include "common.hpp"
#include "detail.hpp"
#include "langdetect_internal.hpp"
#include "modules.hpp"
#include "tugaphone/langdetect.hpp"

namespace parity {

namespace ld = tugaphone::langdetect;
namespace td = tugaphone::detail;

namespace {

std::string scores(const ld::Scores& sc) {
    std::vector<std::string> parts;
    for (const auto& [lang, value] : sc) parts.push_back(list({J(lang), F(value)}));
    return list(parts);
}

const ld::MarkovLangDetector& detector() {
    const ld::MarkovLangDetector* det = ld::get_detector();
    if (det == nullptr) throw std::runtime_error("bundled langdetect models did not load");
    return *det;
}

double margin(const Value& v) { return *tugaphone::py_float(v.str); }

}  // namespace

std::string record_langdetect(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "langs") return "[" + J(fn) + "," + J(ld::langs()) + "]";
    if (fn == "default_margin") return "[" + J(fn) + "," + F(ld::DEFAULT_MARGIN) + "]";
    if (fn == "keep") return "[" + J(fn) + "," + J(ld::portuguese_keep()) + "]";
    if (fn == "from_dir") {
        const auto det = ld::MarkovLangDetector::from_dir(v[1].str);
        return head(v) + (det ? "\"loaded\"" : "null") + "]";
    }
    const std::string& w = v[1].str;
    if (fn == "word") {
        std::vector<std::string> wrapped;
        for (const auto& t : ld::internal::wrap(td::u(w))) wrapped.push_back(td::s8(t));
        return "[" + J(fn) + "," + J(w) + "," + J(td::s8(ld::internal::normalize(td::u(w)))) + "," + J(wrapped) +
               "," + J(ld::fold_diacritics(w)) + "," + J(ld::is_keep_word(w)) + "]";
    }
    if (fn == "score") return head(v) + scores(detector().score(w)) + "]";
    if (fn == "detect") {
        const std::optional<std::string> side = opt_str(v[3]);
        const auto [lang, sc] = detector().detect(w, margin(v[2]), side);
        return head(v) + list({J(lang), scores(sc)}) + "]";
    }
    if (fn == "is_contact") return head(v) + J(detector().is_contact(w, margin(v[2]))) + "]";
    return "unknown call " + fn;
}

}  // namespace parity
