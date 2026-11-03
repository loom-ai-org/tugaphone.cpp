#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <orthography2ipa/orthography2ipa.hpp>

#include "codeswitch_internal.hpp"
#include "detail.hpp"
#include "json.hpp"
#include "o2i_registry.hpp"

namespace tugaphone::codeswitch {

using detail::U32;
using detail::U32View;

namespace {

// Letters Portuguese spelling does not use natively; k w y point at loans.
constexpr std::u32string_view kNonPtLetters = U"kwyñ";
constexpr std::u32string_view kSpanishLetters = U"ñ";
constexpr std::u32string_view kFrenchLetters = U"èëïîûùœæ";
// English digraphs absent from native Portuguese spelling.
const char32_t* const kEnglishDigraphs[] = {U"th", U"sh", U"wh", U"ck", U"gh", U"ght", U"ph", U"ww", U"yy"};

const std::set<std::string> kSpanishStopwords = {
    "el", "los", "las", "del", "por", "para", "con", "una", "unos", "unas",
    "más", "muy", "pero", "como", "este", "esta", "esto", "y", "su", "sus",
};
const std::set<std::string> kFrenchStopwords = {
    "le", "les", "des", "du", "et", "un", "une", "dans", "pour", "avec",
    "sur", "pas", "plus", "très", "mais", "cette", "au", "aux", "je", "vous",
};
const std::set<std::string> kEnglishStopwords = {
    "the", "of", "and", "to", "in", "is", "it", "for", "with", "this",
    "that", "on", "at", "as", "by", "live", "new", "best", "feat", "remix",
    "online", "web", "app", "software", "hardware", "streaming", "stream",
    "download", "upload", "email", "startup", "feedback", "password",
    "weekend", "rock", "pop", "jazz", "band", "single", "album", "site",
    "link", "post", "story", "stories", "reels", "gamer", "gaming", "player",
    "smartphone", "notebook", "mouse", "print", "screenshot", "deadline",
};

bool any_of_letters(U32View core, std::u32string_view letters) {
    return std::any_of(core.begin(), core.end(), [&](char32_t c) { return letters.find(c) != U32View::npos; });
}

bool contact_stopword(const std::string& core) {
    return kSpanishStopwords.count(core) || kFrenchStopwords.count(core) || kEnglishStopwords.count(core);
}

}  // namespace

namespace internal {

const std::set<std::string>& loanwords() {
    static const std::set<std::string> words = [] {
        const json::Value data = json::loads(detail::read_file(detail::data_path("loanwords.json")));
        std::set<std::string> out;
        if (data.kind == json::Value::Object) {
            for (const auto& member : data.members) out.insert(member.first);
        } else if (data.kind == json::Value::Array) {
            for (const auto& item : data.items) {
                if (item.kind != json::Value::String) throw TypeError("unhashable loanword entry");
                out.insert(item.text);
            }
        } else if (data.kind == json::Value::String) {
            for (char32_t c : detail::u(data.text)) out.insert(detail::s8(U32(1, c)));
        } else {
            throw TypeError("loanwords.json is not iterable");
        }
        return out;
    }();
    return words;
}

}  // namespace internal

namespace {

using internal::loanwords;

using Table = std::vector<std::pair<U32, U32>>;

// Stress, liaison and length marks dropped before projection.
const char32_t* const kDrop[] = {U"ˈ", U"ˌ", U"‿", U"ː", U"ˑ"};

// The dict merges of _NATIVIZE_ROMANCE and _NATIVIZE_ENGLISH, in insertion
// order, then sorted by length, longest first, as `sorted(table, key=len,
// reverse=True)` does (a stable sort).
Table sorted_table(Table table) {
    std::stable_sort(table.begin(), table.end(),
                     [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    return table;
}

const Table& common_entries() {
    static const Table table = {{U"ɥ", U"j"}, {U"ə", U"ɐ"}, {U"ɑ", U"a"}, {U"ɜ", U"ɛ"}, {U"ɐ", U"ɐ"}};
    return table;
}

const Table& romance_table() {
    static const Table table = [] {
        Table t = common_entries();
        const Table extra = {
            {U"θ", U"s"}, {U"β", U"b"}, {U"ð", U"d"}, {U"ɣ", U"ɡ"}, {U"ʝ", U"ʒ"}, {U"ɟʝ", U"dʒ"},
            {U"x", U"ʁ"}, {U"ʀ", U"ʁ"}, {U"r", U"ʁ"}, {U"y", U"i"}, {U"ø", U"e"}, {U"œ", U"ɛ"},
            {U"ɑ̃", U"ɐ̃"}, {U"ɛ̃", U"ẽ"}, {U"ɔ̃", U"õ"}, {U"œ̃", U"ẽ"}, {U"æ̃", U"ẽ"},
        };
        t.insert(t.end(), extra.begin(), extra.end());
        return sorted_table(t);
    }();
    return table;
}

const Table& english_table() {
    static const Table table = [] {
        Table t = common_entries();
        const Table extra = {
            {U"eɪ", U"ej"}, {U"aɪ", U"aj"}, {U"ɔɪ", U"ɔj"}, {U"aʊ", U"aw"}, {U"oʊ", U"ow"}, {U"əʊ", U"ow"},
            {U"θ", U"t"}, {U"ð", U"d"},
            {U"ɹ", U"ɾ"}, {U"ɻ", U"ɾ"}, {U"ɫ", U"l"}, {U"ŋ", U"n"}, {U"ʍ", U"w"},
            {U"æ", U"a"}, {U"ʌ", U"a"}, {U"ɒ", U"ɔ"},
            {U"ɪ", U"i"}, {U"ʊ", U"u"}, {U"ɝ", U"ɛ"},
        };
        t.insert(t.end(), extra.begin(), extra.end());
        return sorted_table(t);
    }();
    return table;
}

std::string contact_code(std::string_view contact) {
    if (contact == "es") return "es-ES";
    if (contact == "fr") return "fr-FR";
    if (contact == "en") return "en-US";
    throw KeyError("'" + std::string(contact) + "'");
}

// _contact_engine(code).transcribe(word): one cached orthography2ipa engine
// per contact lattice. Building the engine and transcribing both read the
// orthography2ipa registries, so both hold detail::o2i_registry_mutex().
std::string contact_engine_transcribe(const std::string& code, const std::string& word) {
    const std::lock_guard<std::mutex> lock(detail::o2i_registry_mutex());
    static std::map<std::string, std::unique_ptr<orthography2ipa::G2P>> engines;
    auto& slot = engines[code];
    if (!slot) slot = std::make_unique<orthography2ipa::G2P>(code);
    return slot->transcribe(word);
}

}  // namespace

namespace internal {

std::string strip_token(std::string_view token) {
    const U32 t = detail::u(token);
    U32 chars;
    for (char32_t c : t) {
        if (!(detail::is_alpha(c) || c == U'-')) chars.push_back(c);
    }
    return detail::s8(detail::lower(detail::strip(t, chars)));
}

bool has_english_signal(std::string_view core_utf8) {
    if (kEnglishStopwords.count(std::string(core_utf8))) return true;
    const U32 core = detail::u(core_utf8);
    for (const char32_t* dg : kEnglishDigraphs) {
        if (detail::contains(core, dg)) return true;
    }
    return (detail::contains(core, U'w') || detail::ends_with(core, U"y")) && !any_of_letters(core, kFrenchLetters);
}

std::string nativize(std::string_view ipa_utf8, bool english) {
    const Table& table = english ? english_table() : romance_table();
    U32 ipa = detail::u(ipa_utf8);
    for (const char32_t* d : kDrop) ipa = detail::replace(ipa, d, U"");
    for (const auto& [src, dst] : table) ipa = detail::replace(ipa, src, dst);
    return detail::s8(ipa);
}

std::optional<std::string> classify(std::string_view token, std::string_view default_side,
                                    const langdetect::MarkovLangDetector* detector) {
    if (is_known_loanword(token)) return "en";
    if (detector != nullptr) {
        const std::string lang = detector->detect(token, langdetect::DEFAULT_MARGIN, std::string(default_side)).first;
        if (lang == "pt") return std::nullopt;
        return lang;
    }
    if (!is_contact_word(token)) return std::nullopt;
    const std::string lang = contact_language(token, default_side);
    if (lang == "pt") return std::nullopt;
    return lang;
}

bool is_contact(std::string_view token, const langdetect::MarkovLangDetector* detector) {
    if (is_known_loanword(token)) return true;
    if (detector != nullptr) return detector->is_contact(token);
    return is_contact_word(token);
}

std::vector<Run> split_runs(std::string_view text, std::string_view contact, std::string_view default_side,
                            const langdetect::MarkovLangDetector* detector) {
    std::vector<Run> runs;
    for (const U32& t : detail::split_ws(detail::u(text))) {
        const std::string token = detail::s8(t);
        if (contact == "none") {
            runs.emplace_back(std::nullopt, token);
        } else if (contact == "auto") {
            runs.emplace_back(classify(token, default_side, detector), token);
        } else {
            std::optional<std::string> lang;
            if (is_contact(token, detector)) lang = std::string(contact);
            runs.emplace_back(lang, token);
        }
    }
    return runs;
}

}  // namespace internal

bool is_known_loanword(std::string_view token) { return loanwords().count(internal::strip_token(token)) > 0; }

bool is_contact_word(std::string_view token) {
    const std::string core = internal::strip_token(token);
    if (core.empty()) return false;
    if (langdetect::is_keep_word(core)) return false;
    if (loanwords().count(core)) return true;
    const U32 c32 = detail::u(core);
    if (any_of_letters(c32, kNonPtLetters) || any_of_letters(c32, kFrenchLetters)) return true;
    if (internal::has_english_signal(core)) return true;
    return contact_stopword(core);
}

std::string contact_language(std::string_view token, std::string_view default_side) {
    const std::string core = internal::strip_token(token);
    if (langdetect::is_keep_word(core)) return "pt";
    if (loanwords().count(core)) return "en";
    if (internal::has_english_signal(core)) return "en";
    const U32 c32 = detail::u(core);
    if (any_of_letters(c32, kFrenchLetters) || kFrenchStopwords.count(core)) return "fr";
    if (any_of_letters(c32, kSpanishLetters) || kSpanishStopwords.count(core)) return "es";
    return std::string(default_side);
}

std::string transcribe_contact(std::string_view word, std::string_view contact) {
    const std::string ipa = contact_engine_transcribe(contact_code(contact), std::string(word));
    return internal::nativize(ipa, contact == "en");
}

std::vector<Run> split_runs(std::string_view text, std::string_view contact, std::string_view default_side) {
    const langdetect::MarkovLangDetector* detector =
        (contact == "none") ? nullptr : langdetect::get_detector();
    return internal::split_runs(text, contact, default_side, detector);
}

}  // namespace tugaphone::codeswitch
