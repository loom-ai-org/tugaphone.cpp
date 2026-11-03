#include "codeswitch_internal.hpp"
#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace cs = tugaphone::codeswitch;
namespace ld = tugaphone::langdetect;

namespace {

const ld::MarkovLangDetector* path_detector(const Value& path) {
    if (path.str == "fallback") return nullptr;
    const ld::MarkovLangDetector* det = ld::get_detector();
    if (det == nullptr) throw std::runtime_error("bundled langdetect models did not load");
    return det;
}

std::string run_list(const std::vector<cs::Run>& runs) {
    std::vector<std::string> parts;
    for (const auto& [lang, token] : runs) parts.push_back(list({J(lang), J(token)}));
    return list(parts);
}

}  // namespace

std::string record_codeswitch(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "loanwords") {
        return "[" + J(fn) + "," + J(cs::internal::loanwords()) + "]";
    }
    const std::string& w = v[1].str;
    if (fn == "token") {
        const std::string core = cs::internal::strip_token(w);
        return "[" + J(fn) + "," + J(w) + "," + J(core) + "," + J(cs::is_known_loanword(w)) + "," +
               J(cs::is_contact_word(w)) + "," + J(cs::internal::has_english_signal(w)) + "," +
               J(cs::internal::has_english_signal(core)) + "]";
    }
    if (fn == "contact_language") return head(v) + J(cs::contact_language(w, v[2].str)) + "]";
    if (fn == "nativize") return head(v) + J(cs::internal::nativize(w, v[2].boolean)) + "]";
    if (fn == "classify") return head(v) + J(cs::internal::classify(w, v[2].str, path_detector(v[3]))) + "]";
    if (fn == "is_contact") return head(v) + J(cs::internal::is_contact(w, path_detector(v[2]))) + "]";
    if (fn == "split_runs") {
        return head(v) + run_list(cs::internal::split_runs(w, v[2].str, v[3].str, path_detector(v[4]))) + "]";
    }
    if (fn == "split_runs_public") return head(v) + run_list(cs::split_runs(w, v[2].str, v[3].str)) + "]";
    if (fn == "transcribe_contact") return head(v) + guard([&] { return J(cs::transcribe_contact(w, v[2].str)); }) + "]";
    return "unknown call " + fn;
}

}  // namespace parity
