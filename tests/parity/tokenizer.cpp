#include "tugaphone/tokenizer.hpp"

#include <map>
#include <memory>
#include <sstream>

#include <zlib.h>

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace tk = tugaphone::tokenizer;
namespace dl = tugaphone::dialects;

namespace {

// The dialect named by "<Class>" or "<Class>:<dialect_code>", built once.
tk::DialectPtr dialect(const std::string& spec) {
    static std::map<std::string, tk::DialectPtr> cache;
    auto& slot = cache[spec];
    if (slot) return slot;
    const size_t colon = spec.find(':');
    const std::string name = spec.substr(0, colon);
    const std::optional<std::string> code =
        colon == std::string::npos ? std::nullopt : std::optional<std::string>(spec.substr(colon + 1));
    if (name == "EuropeanPortuguese") slot = std::make_shared<dl::EuropeanPortuguese>(code);
    else if (name == "BrazilianPortuguese") slot = std::make_shared<dl::BrazilianPortuguese>(code);
    else if (name == "AngolanPortuguese") slot = std::make_shared<dl::AngolanPortuguese>(code);
    else if (name == "MozambicanPortuguese") slot = std::make_shared<dl::MozambicanPortuguese>(code);
    else if (name == "TimoresePortuguese") slot = std::make_shared<dl::TimoresePortuguese>(code);
    else if (name == "LisbonPortuguese") slot = std::make_shared<dl::LisbonPortuguese>();
    else if (name == "RioJaneiroPortuguese") slot = std::make_shared<dl::RioJaneiroPortuguese>();
    else if (name == "SaoPauloPortuguese") slot = std::make_shared<dl::SaoPauloPortuguese>();
    else if (name == "DialectInventory") slot = std::make_shared<dl::DialectInventory>();
    else throw std::runtime_error("unknown dialect spec " + spec);
    return slot;
}

std::string guard_all(const std::function<std::string()>& fn) {
    try {
        return fn();
    } catch (const tugaphone::Error& e) {
        return "{\"err\":" + J(e.python_type()) + "}";
    }
}

long code_points(const std::string& s) {
    long n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

template <class T>
std::string surface_of(const T* token) {
    return token ? J(token->surface()) : "null";
}

std::string opt_long(const std::optional<long>& v) { return v ? J(*v) : "null"; }

std::string char_props(const tk::CharToken& c) {
    return list({J(c.surface()), J(c.char_idx()), J(c.idx_in_word()), J(c.idx_in_sentence()), J(c.prefix()),
                 J(c.suffix()), surface_of(c.prev_char()), surface_of(c.next_char()), J(c.parent_syllable()),
                 J(c.is_nucleus()), J(c.repr()), J(c == c.surface())});
}

std::string grapheme_props(const tk::GraphemeToken& g) {
    std::vector<std::string> chars;
    for (const auto& c : g.characters()) chars.push_back(char_props(*c));
    return list({J(g.surface()), J(g.grapheme_idx()), J(g.syllable_idx()), J(g.idx_in_word()),
                 J(g.idx_in_sentence()), J(g.prefix()), J(g.suffix()), surface_of(g.prev_grapheme()),
                 surface_of(g.next_grapheme()), J(g.parent_syllable()), J(g.repr()), list(chars)});
}

std::string word_props(const tk::WordToken& w) {
    std::vector<std::string> group, graphemes;
    for (const auto* x : w.hyphen_group()) group.push_back(x->surface());
    for (const auto& g : w.graphemes()) graphemes.push_back(grapheme_props(*g));
    return list({J(w.surface()), J(w.word_idx()), opt_long(w.chunk_idx()), J(w.normalized()), J(w.syllables()),
                 J(w.normalized_syllables()), J(w.idx_in_sentence()), surface_of(w.prev_word()),
                 surface_of(w.next_word()), J(group), J(w.is_function_word()), J(w.is_hyphen_clitic()),
                 J(w.stress_token()), J(w.repr()), list(graphemes)});
}

std::string crc_len(const std::string& text) {
    const auto crc = crc32(0L, reinterpret_cast<const Bytef*>(text.data()), static_cast<uInt>(text.size()));
    return list({J(static_cast<unsigned long>(crc)), J(code_points(text))});
}

std::string sentence(const Value& v) {
    const std::string spec = v[1].str, text = v[2].str, mode = v[3].str;
    return head(v) + guard_all([&] {
               tk::Sentence s(text, dialect(spec));
               std::vector<std::string> out = {J(s.normalized()), J(s.n_words()), J(s.ipa())};
               const std::string feats = tk::features_json(s.features());
               out.push_back(mode == "full" ? J(feats) : crc_len(feats));
               if (mode != "ipa") {
                   out.push_back(J(s.repr()));
                   std::vector<std::string> words;
                   for (const auto& w : s.words()) words.push_back(word_props(*w));
                   out.push_back(mode == "full" ? list(words) : crc_len(list(words)));
               }
               return list(out);
           }) + "]";
}

std::string word(const Value& v) {
    const std::string spec = v[1].str, surface = v[2].str;
    const auto syllables = v[3].strings();
    const std::optional<long> chunk = v[4].is_null() ? std::nullopt : std::optional<long>(v[4].integer);
    return head(v) + guard_all([&] {
               tk::WordToken w(surface, 0, syllables, nullptr, dialect(spec), chunk);
               return list({J(w.ipa()), J(tk::features_json(w.features())), word_props(w), J(w.stress_pattern()),
                            J(w.stressed_syllable_idx())});
           }) + "]";
}

std::string manual(const Value& v) {
    const std::string spec = v[1].str;
    const auto surfaces = v[2].strings();
    return head(v) + guard_all([&] {
               std::vector<std::unique_ptr<tk::WordToken>> words;
               std::string text;
               for (size_t i = 0; i < surfaces.size(); ++i) {
                   words.push_back(std::make_unique<tk::WordToken>(
                       surfaces[i], static_cast<long>(i), std::vector<std::string>{}, nullptr, dialect(spec),
                       i < 2 ? std::optional<long>(0) : std::nullopt));
                   text += (i ? " " : "") + surfaces[i];
               }
               tk::Sentence s(text, std::move(words), dialect("BrazilianPortuguese"));
               std::vector<std::string> props;
               for (const auto& w : s.words()) props.push_back(word_props(*w));
               return list({J(s.ipa()), J(tk::features_json(s.features())), list(props)});
           }) + "]";
}

std::string grapheme(const Value& v) {
    const std::string surface = v[1].str;
    return head(v) + guard_all([&] {
               tk::GraphemeToken g(surface, static_cast<long>(v[2].integer), static_cast<long>(v[3].integer));
               return list({J(g.normalized()), J(g.n_chars()), J(g.prefix()), J(g.suffix()),
                            surface_of(g.prev_grapheme()), surface_of(g.next_grapheme()), J(g.parent_syllable()),
                            guard_all([&] { return J(g.is_vowel_grapheme()); }), guard_all([&] { return J(g.ipa()); }),
                            guard_all([&] { return J(g.is_archaism()); }), guard_all([&] {
                                std::vector<std::string> chars;
                                for (const auto& c : g.characters()) chars.push_back(char_props(*c));
                                return list(chars);
                            })});
           }) + "]";
}

std::string char_token(const Value& v) {
    tk::GraphemeToken g("a", 0, 0);
    return head(v) + guard_all([&] {
               tk::CharToken c(v[1].str, 0, &g);
               return char_props(c);
           }) + "]";
}

}  // namespace

std::string record_tokenizer(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "sentence") return sentence(v);
    if (fn == "word") return word(v);
    if (fn == "manual") return manual(v);
    if (fn == "grapheme") return grapheme(v);
    if (fn == "char") return char_token(v);
    if (fn == "PRONOUNCED_QU_GU_U") return "[" + J(fn) + "," + J(tk::PRONOUNCED_QU_GU_U()) + "]";
    if (fn == "pronounces_qu_gu_u") return head(v) + guard_all([&] { return J(tk::pronounces_qu_gu_u(v[1].str)); }) + "]";
    if (fn == "is_grapheme_silent") {
        return head(v) + guard_all([&] {
                   return J(tk::is_grapheme_silent(v[2].str, v[3].str, v[4].str, v[5].str, *dialect(v[1].str)));
               }) + "]";
    }
    if (fn == "detect_stress_position") {
        return head(v) + guard_all([&] {
                   return J(tk::detect_stress_position(v[2].str, v[3].strings(), *dialect(v[1].str)));
               }) + "]";
    }
    if (fn == "demonstrate") {
        std::ostringstream out;
        tk::demonstrate_transcription(out, v[2].str, dialect(v[1].str));
        return head(v) + J(out.str()) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
