// The records call the deprecated process-wide lexicon API on purpose.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "tugaphone/accent.hpp"
#include "tugaphone/lattice_core.hpp"

#include <filesystem>
#include <fstream>
#include <optional>

#include <unistd.h>

#include <orthography2ipa/orthography2ipa.hpp>

#include "common.hpp"
#include "detail.hpp"
#include "modules.hpp"

namespace parity {

namespace lc = tugaphone::lattice_core;
namespace fs = std::filesystem;
namespace o2i = orthography2ipa;

namespace {

std::string fake_contact(const std::string& token, const std::string& lang) {
    return token.rfind('x', 0) == 0 ? "" : "<" + lang + ":" + token + ">";
}

// [[surface, ipa], ...] of engine(lect).sentence_lattice(text).
std::string engine_lattice(const std::string& text, const std::string& lect) {
    std::string out = "[";
    bool first = true;
    for (const auto& word : lc::engine(lect).sentence_lattice(text).words) {
        out += (first ? "[" : ",[") + J(word.surface) + "," + J(word.ipa) + "]";
        first = false;
    }
    return out + "]";
}

// [[word, [[grapheme, top1_ipa], ...]], ...] of engine(lect).features(text).
std::string engine_features(const std::string& text, const std::string& lect) {
    std::string out = "[";
    bool first_word = true;
    for (const auto& word : lc::engine(lect).features(text)) {
        out += (first_word ? "[" : ",[") + J(word.word) + ",[";
        bool first = true;
        for (const auto& g : word.graphemes) {
            out += (first ? "[" : ",[") + J(g.grapheme) + "," + J(g.top1_ipa) + "]";
            first = false;
        }
        out += "]]";
        first_word = false;
    }
    return out + "]";
}

// sorted(engine(lect).pipeline.lexicon.items()).
std::string engine_lexicon(const std::string& lect) {
    const auto& lexicon = lc::engine(lect).pipeline().lexicon;
    std::string out = "[";
    bool first = true;
    for (const auto& [word, ipa] : lexicon.value()) {
        out += (first ? "[" : ",[") + J(word) + "," + J(ipa) + "]";
        first = false;
    }
    return out + "]";
}

// [plain, engine]: a lexicon registered for the whole process reaches a plain
// G2P and does not reach the engine, which holds its own lexicon.
std::string process_lexicon(const std::string& lect, const std::string& word) {
    const fs::path path = fs::temp_directory_path() / ("tugaphone-parity-" + std::to_string(getpid()) + ".tsv");
    std::ofstream(path, std::ios::binary) << word << "\tˈzzz\n";
    o2i::register_lexicon(lect, path.string());
    lc::clear_caches();
    const std::string plain = o2i::G2P(lect).transcribe_word(word);
    const std::string engine = lc::phonemize(word, lect);
    o2i::clear_lexicons();
    lc::clear_caches();
    fs::remove(path);
    return "[" + J(plain) + "," + J(engine) + "]";
}

}  // namespace

std::string record_lattice_core(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "phonemize") {
        return head(v) + guard([&] { return J(lc::phonemize(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "runs") {
        std::vector<lc::Run> runs;
        for (const auto& r : v[2].items) runs.emplace_back(opt_str(r[0]), r[1].str);
        return head(v) + guard([&] {
                   lc::validate_contact("en");
                   return J(lc::phonemize_runs(v[1].str, runs, fake_contact));
               }) + "]";
    }
    if (fn == "respell_word") {
        return head(v) + guard([&] { return J(tugaphone::accent::respell_word(v[1].str, v[2].str)); }) + "]";
    }
    if (fn == "force_accent") {
        return head(v) + guard([&] { return J(tugaphone::accent::force_accent(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "normalize") return head(v) + guard([&] { return J(lc::normalize(v[1].str, v[2].str)); }) + "]";
    if (fn == "VALID_CONTACT") return "[" + J(fn) + "," + J(lc::VALID_CONTACT()) + "]";
    if (fn == "validate_contact") {
        return head(v) + guard([&] {
                   lc::validate_contact(v[1].str);
                   return std::string("null");
               }) + "]";
    }
    if (fn == "engine_lattice") return head(v) + guard([&] { return engine_lattice(v[1].str, v[2].str); }) + "]";
    if (fn == "engine_features") return head(v) + guard([&] { return engine_features(v[1].str, v[2].str); }) + "]";
    if (fn == "engine_lexicon") return head(v) + guard([&] { return engine_lexicon(v[1].str); }) + "]";
    if (fn == "plain_engine") {
        return head(v) + guard([&] { return J(o2i::G2P(v[1].str).transcribe_word(v[2].str)); }) + "]";
    }
    if (fn == "process_lexicon") return head(v) + process_lexicon(v[1].str, v[2].str) + "]";
    return "unknown call " + fn;
}

}  // namespace parity
