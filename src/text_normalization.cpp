// Each regular expression of text_normalization.py is ported as explicit
// code that tries the same alternatives in the same order as Python's
// backtracking engine, so every match and every replacement is the same.
#include "tugaphone/text_normalization.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include <tugalex/tugalex.hpp>

#include "detail.hpp"
#include "json.hpp"
#include "tugaphone/number_utils.hpp"

namespace tugaphone::text_normalization {

using detail::from_utf8;
using detail::to_utf8;
using detail::U32;
using detail::U32View;
using number_utils::NumberParser;

namespace {

// ── Pattern pieces ──────────────────────────────────────────────────────────

bool is_d(char32_t c) { return detail::is_decimal(c); }  // \d
bool is_w(char32_t c) { return detail::is_word(c); }     // \w

bool d_at(U32View t, size_t i) { return i < t.size() && is_d(t[i]); }
bool w_at(U32View t, size_t i) { return i < t.size() && is_w(t[i]); }
bool char_at(U32View t, size_t i, char32_t c) { return i < t.size() && t[i] == c; }
bool one_of_at(U32View t, size_t i, U32View set) { return i < t.size() && set.find(t[i]) != U32View::npos; }

// The end of the run of \d that starts at i.
size_t digit_run(U32View t, size_t i) {
    while (i < t.size() && is_d(t[i])) ++i;
    return i;
}

// Every end of \d+(?:[.,]\d+)* from p, in the order the engine tries them.
// The engine always backs off one code point at a time, so that order is the
// descending order of the valid ends.
std::vector<size_t> number_ends(U32View t, size_t p, U32View separators) {
    std::vector<size_t> ends;
    size_t start = p;
    while (true) {
        const size_t e = digit_run(t, start);
        if (e == start) break;
        for (size_t k = start + 1; k <= e; ++k) ends.push_back(k);
        if (one_of_at(t, e, separators) && d_at(t, e + 1)) {
            start = e + 1;
        } else {
            break;
        }
    }
    return {ends.rbegin(), ends.rend()};
}

// \b at position i.
bool boundary(U32View t, size_t i) {
    const bool before = i > 0 && is_w(t[i - 1]);
    const bool after = w_at(t, i);
    return before != after;
}

// (?<![\w.,]) at position p.
bool not_after_word_or_mark(U32View t, size_t p) {
    if (p == 0) return true;
    const char32_t c = t[p - 1];
    return !(is_w(c) || c == '.' || c == ',');
}

U32 slice(U32View t, size_t b, size_t e) { return U32(t.substr(b, e - b)); }

U32 ascii(long long v) { return from_utf8(std::to_string(v)); }

long long int_of(U32View digits) { return std::stoll(py_int(to_utf8(digits))->str()); }

// ── Unit symbols ────────────────────────────────────────────────────────────

// sorted(NumberParser.UNIT_NAMES, key=len, reverse=True): longest first,
// ties in table order.
const std::vector<U32>& unit_symbols() {
    static const std::vector<U32> symbols = [] {
        std::vector<U32> out;
        for (const auto& unit : NumberParser::UNIT_NAMES()) out.push_back(from_utf8(unit.symbol));
        std::stable_sort(out.begin(), out.end(), [](const U32& a, const U32& b) { return a.size() > b.size(); });
        return out;
    }();
    return symbols;
}

// _GLUED_UNIT: (?<![\w.,])(NUMBER)(SYMBOLS)(?![\w/²³]) -> \1 \2
U32 glued_units(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (not_after_word_or_mark(t, p)) {
            for (size_t end : number_ends(t, p, U".,")) {
                for (const U32& symbol : unit_symbols()) {
                    if (t.compare(end, symbol.size(), symbol) != 0) continue;
                    const size_t after = end + symbol.size();
                    if (w_at(t, after) || one_of_at(t, after, U"/²³")) continue;
                    out += slice(t, p, end) + U" " + symbol;
                    p = after;
                    matched = true;
                    break;
                }
                if (matched) break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

// _EURO_FIRST: € ?(NUMBER)(?!\w) -> \1 €
U32 euro_first(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (t[p] == U'€') {
            // " ?" tries the space first.
            std::vector<size_t> starts;
            if (char_at(t, p + 1, U' ')) starts.push_back(p + 2);
            starts.push_back(p + 1);
            for (size_t q : starts) {
                for (size_t end : number_ends(t, q, U".,")) {
                    if (w_at(t, end)) continue;
                    out += slice(t, q, end) + U" €";
                    p = end;
                    matched = true;
                    break;
                }
                if (matched) break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

U32 euro_cents_repl(const U32& euros, const U32& cents_digits) {
    U32 padded = cents_digits;
    while (padded.size() < 2) padded.push_back(U'0');
    const long long cents = int_of(padded);
    if (cents == 0) return euros + U" €";
    const U32 cents_text = ascii(cents) + (cents == 1 ? U" cêntimo" : U" cêntimos");
    if (py_int(to_utf8(detail::replace(euros, U".", U"")))->is_zero()) return cents_text;
    return euros + U" € e " + cents_text;
}

// _EURO_CENTS: (?<![\w.,])(\d+(?:\.\d{3})*),(\d{1,2}) €
U32 euro_cents(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (not_after_word_or_mark(t, p) && d_at(t, p)) {
            // The ends of \d+(?:\.\d{3})*, longest first.
            std::vector<size_t> ends;
            const size_t e0 = digit_run(t, p);
            size_t e = e0;
            std::vector<size_t> groups;
            while (char_at(t, e, U'.') && d_at(t, e + 1) && d_at(t, e + 2) && d_at(t, e + 3)) {
                e += 4;
                groups.push_back(e);
            }
            for (auto it = groups.rbegin(); it != groups.rend(); ++it) ends.push_back(*it);
            for (size_t k = e0; k > p; --k) ends.push_back(k);
            for (size_t end : ends) {
                if (!char_at(t, end, U',')) continue;
                for (size_t width : {size_t{2}, size_t{1}}) {
                    bool digits = true;
                    for (size_t k = 0; k < width; ++k) digits = digits && d_at(t, end + 1 + k);
                    if (!digits) continue;
                    const size_t q = end + 1 + width;
                    if (t.compare(q, 2, U" €") != 0) continue;
                    out += euro_cents_repl(slice(t, p, end), slice(t, end + 1, q));
                    p = q + 2;
                    matched = true;
                    break;
                }
                if (matched) break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

// The lookahead (?=\d+(?:,\d+)?(?:[x×]\d|(?!\w))) at r.
bool times_lookahead(U32View t, size_t r) {
    for (size_t end : number_ends(t, r, U",")) {
        // number_ends allows more than one ",\d+" group; keep those with at most one.
        size_t commas = 0;
        for (size_t k = r; k < end; ++k) commas += t[k] == U',';
        if (commas > 1) continue;
        if (one_of_at(t, end, U"x×") && d_at(t, end + 1)) return true;
        if (!w_at(t, end)) return true;
    }
    return false;
}

// One pass of _TIMES: (?<![\w.,])(\d+(?:,\d+)?) ?[x×] ?(?=...) -> \1 vezes
U32 times_once(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (not_after_word_or_mark(t, p) && d_at(t, p)) {
            std::vector<size_t> ends;
            const size_t e0 = digit_run(t, p);
            if (char_at(t, e0, U',') && d_at(t, e0 + 1)) {
                const size_t e1 = digit_run(t, e0 + 1);
                for (size_t k = e1; k >= e0 + 2; --k) ends.push_back(k);
            }
            for (size_t k = e0; k > p; --k) ends.push_back(k);
            for (size_t end : ends) {
                std::vector<size_t> xs;
                if (char_at(t, end, U' ')) xs.push_back(end + 1);
                xs.push_back(end);
                for (size_t q : xs) {
                    if (!one_of_at(t, q, U"x×")) continue;
                    std::vector<size_t> rs;
                    if (char_at(t, q + 1, U' ')) rs.push_back(q + 2);
                    rs.push_back(q + 1);
                    for (size_t r : rs) {
                        if (!times_lookahead(t, r)) continue;
                        out += slice(t, p, end) + U" vezes ";
                        p = r;
                        matched = true;
                        break;
                    }
                    if (matched) break;
                }
                if (matched) break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

// ── Clock times ─────────────────────────────────────────────────────────────

// The ends of (?:[01]?\d|2[0-N]) at p, in the order the engine tries them.
std::vector<size_t> hour_ends(U32View t, size_t p, char32_t last_twenty) {
    std::vector<size_t> ends;
    if (one_of_at(t, p, U"01") && d_at(t, p + 1)) ends.push_back(p + 2);
    if (d_at(t, p)) ends.push_back(p + 1);
    if (char_at(t, p, U'2') && p + 1 < t.size() && t[p + 1] >= U'0' && t[p + 1] <= last_twenty) ends.push_back(p + 2);
    return ends;
}

const U32 kClockPunct = U".,;:!?)\"”…";

// _WHOLE_HOUR: \b(HOUR_OR_24):00(PUNCT?)
U32 whole_hours(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (boundary(t, p)) {
            for (size_t end : hour_ends(t, p, U'4')) {
                if (t.compare(end, 3, U":00") != 0) continue;
                size_t q = end + 3;
                U32 punct;
                if (one_of_at(t, q, kClockPunct)) punct = U32(1, t[q++]);
                const U32 hh = slice(t, p, end);
                out += hh + (int_of(hh) == 1 ? U" hora" : U" horas") + punct;
                p = q;
                matched = true;
                break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

// _CLOCK_TIME: \b(HOUR):([0-5]\d)(PUNCT?)
U32 clock_times(U32View t) {
    U32 out;
    size_t p = 0;
    while (p < t.size()) {
        bool matched = false;
        if (boundary(t, p)) {
            for (size_t end : hour_ends(t, p, U'3')) {
                if (!char_at(t, end, U':')) continue;
                if (!(end + 1 < t.size() && t[end + 1] >= U'0' && t[end + 1] <= U'5' && d_at(t, end + 2))) continue;
                size_t q = end + 3;
                U32 punct;
                if (one_of_at(t, q, kClockPunct)) punct = U32(1, t[q++]);
                out += slice(t, p, end) + U" e " + ascii(int_of(slice(t, end + 1, end + 3))) + punct;
                p = q;
                matched = true;
                break;
            }
        }
        if (!matched) out.push_back(t[p++]);
    }
    return out;
}

// ── Word tables ─────────────────────────────────────────────────────────────

const std::map<U32, U32>& unconditional_abbreviations() {
    static const std::map<U32, U32> m = {
        {U"vs.", U"versus"},          {U"pág.", U"página"},        {U"págs.", U"páginas"},
        {U"tel.", U"telefone"},       {U"art.", U"artigo"},        {U"fig.", U"figura"},
        {U"cap.", U"capítulo"},       {U"séc.", U"século"},        {U"Av.", U"Avenida"},
        {U"R.", U"Rua"},              {U"Lx.", U"Lisboa"},         {U"n.º", U"número"},
        {U"nº", U"número"},           {U"N.º", U"número"},         {U"Nº", U"número"},
        {U"n.os", U"números"},        {U"N.os", U"números"},       {U"Sr.ª", U"Senhora"},
        {U"Sr.as", U"Senhoras"},      {U"Dr.ª", U"Doutora"},       {U"Eng.ª", U"Engenheira"},
        {U"Prof.ª", U"Professora"},   {U"Ex.ª", U"Excelência"},    {U"Ex.as", U"Excelências"},
        {U"Exa.", U"Excelência"},     {U"Exas.", U"Excelências"},  {U"Ex.mo", U"Excelentíssimo"},
        {U"Ex.ma", U"Excelentíssima"}, {U"Ex.mos", U"Excelentíssimos"}, {U"Ex.mas", U"Excelentíssimas"},
        {U"Exmo.", U"Excelentíssimo"}, {U"Exma.", U"Excelentíssima"}, {U"Exmos.", U"Excelentíssimos"},
        {U"Exmas.", U"Excelentíssimas"},
        {U"Sr.", U"Senhor"},          {U"Sra.", U"Senhora"},       {U"Srs.", U"Senhores"},
        {U"Sras.", U"Senhoras"},      {U"Dr.", U"Doutor"},         {U"Dra.", U"Doutora"},
        {U"Eng.", U"Engenheiro"},     {U"Prof.", U"Professor"},
    };
    return m;
}

const std::set<U32> kLetterLabelNouns = {U"vitamina", U"letra", U"alínea", U"classe", U"grupo", U"tipo", U"plano"};

const std::map<U32, std::pair<U32, U32>> kPossessives = {
    {U"V.", {U"Vossa", U"Vossas"}},
    {U"VV.", {U"Vossas", U"Vossas"}},
    {U"Vv.", {U"Vossas", U"Vossas"}},
    {U"S.", {U"Sua", U"Suas"}},
};
const std::map<U32, bool> kExcelenciaPlural = {{U"Ex.as", true}, {U"Exas.", true}, {U"Ex.ª", false}, {U"Exa.", false}};
const U32 kAbbreviationTrail = U",;:!?)\"”";
const U32 kLeadPunct = U"(“\"'¿¡";
const U32 kTrailPunct = U".,;:!?)\"”";
const U32 kTrailPunctWide = U".,;:!?)\"”…";

bool is_capitalised(U32View word) {
    const U32 core = detail::lstrip(word, kLeadPunct);
    return !core.empty() && detail::is_upper(core[0]);
}

std::optional<std::string> given_name_gender(U32View word) {
    return tugalex::TugaLexicon::get_given_name_gender(to_utf8(word));
}

// _name_gender: a feminine given name gives feminine, anything else masculine.
std::string name_gender(U32View word) {
    const auto gender = given_name_gender(word);
    return gender && *gender == "f" ? "feminine" : "masculine";
}

// _split_edges: (leading punctuation, core, trailing punctuation).
struct Edges {
    U32 lead, core, trail;
};
Edges split_edges(U32View w) {
    size_t i = 0, j = w.size();
    while (i < j && kLeadPunct.find(w[i]) != U32::npos) ++i;
    while (j > i && kTrailPunctWide.find(w[j - 1]) != U32::npos) --j;
    return {slice(w, 0, i), slice(w, i, j), slice(w, j, w.size())};
}

// _split_trailing_punct.
std::pair<U32, U32> split_trailing_punct(U32View w) {
    size_t i = w.size();
    while (i > 0 && kTrailPunct.find(w[i - 1]) != U32::npos) --i;
    return {slice(w, 0, i), slice(w, i, w.size())};
}

bool is_brazilian(std::string_view lang) {
    return detail::contains(detail::lower(from_utf8(lang)), U"pt-br");
}

// ── Roman numerals ──────────────────────────────────────────────────────────

const std::map<U32, std::string> kWorkParts = {
    {U"século", "masculine"}, {U"capítulo", "masculine"}, {U"tomo", "masculine"}, {U"volume", "masculine"},
    {U"livro", "masculine"},  {U"canto", "masculine"},    {U"ato", "masculine"},  {U"título", "masculine"},
    {U"parte", "feminine"},   {U"secção", "feminine"},    {U"seção", "feminine"}};

const std::map<U32, std::string> kPrenominal = {
    {U"o", "masculine"},   {U"os", "masculine"},   {U"do", "masculine"},   {U"dos", "masculine"},
    {U"no", "masculine"},  {U"nos", "masculine"},  {U"ao", "masculine"},   {U"aos", "masculine"},
    {U"pelo", "masculine"}, {U"pelos", "masculine"},
    {U"a", "feminine"},    {U"as", "feminine"},    {U"da", "feminine"},    {U"das", "feminine"},
    {U"na", "feminine"},   {U"nas", "feminine"},   {U"à", "feminine"},     {U"às", "feminine"},
    {U"pela", "feminine"}, {U"pelas", "feminine"}};

// _letter_expressions: (lowercase word, letter) pairs, read once per path.
const std::set<std::pair<U32, U32>>& letter_expressions() {
    static std::mutex mutex;
    static std::map<std::string, std::set<std::pair<U32, U32>>> cache;
    const std::string path = detail::data_path("letter_expressions.json");
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    const json::Value doc = json::loads(detail::read_file(path));
    std::set<std::pair<U32, U32>> pairs;
    for (const auto& phrase : doc.at("expressions").items) {
        const U32 text = from_utf8(phrase.text);
        const size_t space = text.rfind(U' ');
        if (space == U32::npos) throw ValueError("not enough values to unpack (expected 2, got 1)");
        pairs.emplace(detail::lower(text.substr(0, space)), text.substr(space + 1));
    }
    return cache.emplace(path, std::move(pairs)).first->second;
}

// _ROMAN.fullmatch: M{0,3}(?:CM|CD|D?C{0,3})(?:XC|XL|L?X{0,3})(?:IX|IV|V?I{0,3})
// One decimal place of the numeral: the ends that `nine`, `four` or
// `five?one{0,3}` can reach from i.
std::vector<size_t> roman_place(U32View s, size_t i, char32_t one, char32_t five, char32_t ten) {
    std::vector<size_t> ends;
    if (char_at(s, i, one) && char_at(s, i + 1, ten)) ends.push_back(i + 2);
    if (char_at(s, i, one) && char_at(s, i + 1, five)) ends.push_back(i + 2);
    for (size_t start : {i + (char_at(s, i, five) ? 1 : 0), i}) {
        size_t k = start;
        ends.push_back(k);
        for (int n = 0; n < 3 && char_at(s, k, one); ++n) ends.push_back(++k);
        if (start == i) break;
    }
    return ends;
}

bool roman_fullmatch(U32View s) {
    size_t m = 0;
    while (m < 3 && char_at(s, m, U'M')) ++m;
    for (size_t a = 0; a <= m; ++a) {
        for (size_t b : roman_place(s, a, U'C', U'D', U'M')) {
            for (size_t c : roman_place(s, b, U'X', U'L', U'C')) {
                for (size_t d : roman_place(s, c, U'I', U'V', U'X')) {
                    if (d == s.size()) return true;
                }
            }
        }
    }
    return false;
}

std::optional<long long> roman_value(U32View numeral) {
    if (numeral.empty() || !roman_fullmatch(numeral)) return std::nullopt;
    auto value_of = [](char32_t c) -> long long {
        switch (c) {
            case U'I': return 1;
            case U'V': return 5;
            case U'X': return 10;
            case U'L': return 50;
            case U'C': return 100;
            case U'D': return 500;
            default: return 1000;
        }
    };
    long long total = 0;
    for (size_t i = 0; i < numeral.size(); ++i) {
        const long long v = value_of(numeral[i]);
        const long long next = i + 1 < numeral.size() ? value_of(numeral[i + 1]) : 0;
        total += v < next ? -v : v;
    }
    return total;
}

size_t alpha_count(U32View w) {
    size_t n = 0;
    for (char32_t c : w) n += detail::is_alpha(c);
    return n;
}

bool is_caps_word(U32View core) {
    bool any_alpha = false;
    for (char32_t c : core) any_alpha = any_alpha || detail::is_alpha(c);
    return any_alpha && U32(core) == detail::upper(core);
}

std::pair<size_t, size_t> caps_run(const std::vector<U32>& cores, size_t i) {
    size_t start = i, end = i;
    while (start > 0 && is_caps_word(cores[start - 1])) --start;
    while (end + 1 < cores.size() && is_caps_word(cores[end + 1])) ++end;
    return {start, end};
}

bool in_caps_run(const std::vector<U32>& cores, size_t i) {
    const auto [start, end] = caps_run(cores, i);
    for (size_t k = start; k <= end; ++k) {
        if (alpha_count(cores[k]) >= 2 && !roman_value(cores[k])) return true;
    }
    return false;
}

bool converts(U32View numeral, const std::optional<U32>& prev_word, const std::vector<U32>& cores, size_t i) {
    if (prev_word && letter_expressions().count({detail::lower(*prev_word), U32(numeral)})) return false;
    if (prev_word && kWorkParts.count(detail::lower(*prev_word))) return true;
    if (in_caps_run(cores, i)) return false;
    for (char32_t c : numeral) {
        if (c != U'I' && c != U'V' && c != U'X') return false;
    }
    return numeral.size() >= 2 || (prev_word && given_name_gender(*prev_word).has_value());
}

std::string read_roman(long long value, const std::optional<U32>& prev_word, std::string_view lang) {
    std::optional<std::string> article;
    if (prev_word && !prev_word->empty()) {
        auto it = kPrenominal.find(detail::lower(*prev_word));
        if (it != kPrenominal.end()) article = it->second;
    } else {
        article = "masculine";
    }
    std::string gender;
    bool as_ordinal;
    if (article) {
        gender = *article;
        as_ordinal = true;
    } else if (value <= 10) {
        auto it = kWorkParts.find(detail::lower(*prev_word));
        gender = it != kWorkParts.end() ? it->second : name_gender(*prev_word);
        as_ordinal = true;
    } else {
        gender = "masculine";
        as_ordinal = false;
    }
    return NumberParser::pronounce_number_word(std::to_string(value), std::nullopt, std::nullopt, gender, as_ordinal,
                                               is_brazilian(lang));
}

U32 title(U32View reading) {
    std::vector<U32> words;
    for (const U32& w : detail::split_ws(reading)) words.push_back(w == U"e" ? w : detail::capitalize(w));
    return detail::join(words, U" ");
}

// ── Acronyms ────────────────────────────────────────────────────────────────

const std::map<char32_t, U32> kLetterNames = {
    {U'A', U"á"},   {U'B', U"bê"},   {U'C', U"cê"},   {U'D', U"dê"},     {U'E', U"é"},    {U'F', U"éfe"},
    {U'G', U"gê"},  {U'H', U"agá"},  {U'I', U"i"},    {U'J', U"jota"},   {U'K', U"capa"}, {U'L', U"éle"},
    {U'M', U"éme"}, {U'N', U"éne"},  {U'O', U"ó"},    {U'P', U"pê"},     {U'Q', U"quê"},  {U'R', U"érre"},
    {U'S', U"ésse"}, {U'T', U"tê"},  {U'U', U"u"},    {U'V', U"vê"},     {U'W', U"dâblio"}, {U'X', U"xis"},
    {U'Y', U"ípsilon"}, {U'Z', U"zê"}};

U32 spell_letters(U32View acronym) {
    std::vector<U32> names;
    for (char32_t c : acronym) names.push_back(kLetterNames.at(c));
    return detail::join(names, U" ");
}

const std::map<U32, U32>& acronyms() {
    static const std::map<U32, U32> m = [] {
        std::map<U32, U32> out = {{U"IA", U"i á"},   {U"LLM", U"éle éle éme"}, {U"UTC", U"u tê cê"},
                                  {U"TTS", U"tê tê ésse"}, {U"PT", U"pê tê"}, {U"EU", U"e u"}};
        for (const U32 a : {U"GPS", U"USB", U"PDF", U"SMS", U"CPU", U"URL", U"API", U"IVA", U"IRS", U"NIF", U"SNS",
                            U"PS", U"PSD"}) {
            out[a] = spell_letters(a);
        }
        return out;
    }();
    return m;
}

const std::set<U32> kWordAcronyms = {U"IA", U"EU"};

bool in_caps_text(const std::vector<U32>& cores, size_t i) {
    const auto [start, end] = caps_run(cores, i);
    for (size_t j = start; j <= end; ++j) {
        if (j != i && alpha_count(cores[j]) >= 2) return true;
    }
    return false;
}

bool ascii_upper(char32_t c) { return c >= U'A' && c <= U'Z'; }
bool ascii_lower(char32_t c) { return c >= U'a' && c <= U'z'; }

// _LETTER_DIGIT_CODE.fullmatch: ([A-Z]{1,3}|[a-z]{1,2})(\d+)
std::optional<std::pair<U32, U32>> letter_digit_code(U32View core) {
    using Run = std::pair<bool (*)(char32_t), size_t>;
    for (const auto& [letter, most] : {Run{ascii_upper, 3}, Run{ascii_lower, 2}}) {
        size_t k = 0;
        while (k < core.size() && k < most && letter(core[k])) ++k;
        for (; k >= 1; --k) {
            const U32View digits = core.substr(k);
            bool ok = !digits.empty();
            for (char32_t c : digits) ok = ok && is_d(c);
            if (ok) return std::make_pair(U32(core.substr(0, k)), U32(digits));
        }
    }
    return std::nullopt;
}

}  // namespace

std::string split_number_ranges(std::string_view text) {
    const U32 t = from_utf8(text);
    U32 out;
    for (size_t i = 0; i < t.size(); ++i) {
        const bool dash = U32View(U"-–—−").find(t[i]) != U32View::npos;
        if (dash && i > 0 && is_d(t[i - 1]) && d_at(t, i + 1)) {
            out += U" a ";
        } else {
            out.push_back(t[i]);
        }
    }
    return to_utf8(out);
}

std::string normalize_number_separators(std::string_view text) {
    const U32 t = from_utf8(text);
    U32 dropped;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == U'.' && i > 0 && is_d(t[i - 1]) && d_at(t, i + 1)) continue;
        dropped.push_back(t[i]);
    }
    U32 out;
    for (size_t i = 0; i < dropped.size(); ++i) {
        if (dropped[i] == U',' && i > 0 && is_d(dropped[i - 1]) && d_at(dropped, i + 1)) {
            out += U" vírgula ";
        } else {
            out.push_back(dropped[i]);
        }
    }
    return to_utf8(out);
}

std::string separate_unit_symbols(std::string_view text) {
    U32 t = euro_cents(euro_first(glued_units(from_utf8(text))));
    while (true) {
        U32 expanded = times_once(t);
        if (expanded == t) return to_utf8(t);
        t = std::move(expanded);
    }
}

std::string expand_clock_times(std::string_view text) { return to_utf8(clock_times(whole_hours(from_utf8(text)))); }

std::string expand_abbreviations(std::string_view text) {
    const std::vector<U32> words = detail::split_ws(from_utf8(text));
    std::vector<U32> cores;
    for (const U32& w : words) cores.push_back(detail::rstrip(w, kAbbreviationTrail));
    std::vector<U32> out;
    for (size_t i = 0; i < words.size(); ++i) {
        const U32& w = words[i];
        const U32& core = cores[i];
        const U32 trail = w.substr(core.size());
        const std::optional<U32> following = i + 1 < words.size() ? std::optional<U32>(cores[i + 1]) : std::nullopt;
        auto possessive = kPossessives.find(core);
        if (possessive != kPossessives.end() && following && kExcelenciaPlural.count(*following)) {
            const bool plural = kExcelenciaPlural.at(*following);
            out.push_back((plural ? possessive->second.second : possessive->second.first) + trail);
            continue;
        }
        auto unconditional = unconditional_abbreviations().find(core);
        if (unconditional != unconditional_abbreviations().end()) {
            out.push_back(unconditional->second + trail);
            continue;
        }
        const bool honorific = core == U"D.";
        const bool after_label = i > 0 && kLetterLabelNouns.count(detail::lower(cores[i - 1]));
        if (honorific && following && is_capitalised(words[i + 1]) && !after_label) {
            const U32 expansion = name_gender(split_edges(*following).core) == "feminine" ? U"Dona" : U"Dom";
            out.push_back(expansion + trail);
            continue;
        }
        out.push_back(w);
    }
    return to_utf8(detail::join(out, U" "));
}

std::string expand_regnal_numerals(std::string_view text, std::string_view lang) {
    const std::vector<U32> words = detail::split_ws(from_utf8(text));
    std::vector<U32> cores;
    for (const U32& w : words) cores.push_back(split_edges(w).core);
    std::vector<U32> out;
    for (size_t i = 0; i < words.size(); ++i) {
        const U32& w = words[i];
        const auto [core, trail] = split_trailing_punct(w);
        const auto value = roman_value(core);
        const std::optional<U32> prev_core =
            i > 0 ? std::optional<U32>(split_trailing_punct(words[i - 1]).first) : std::nullopt;
        if (!value || !converts(core, prev_core, cores, i)) {
            out.push_back(w);
            continue;
        }
        const U32 reading = from_utf8(read_roman(*value, prev_core, lang));
        bool capitalised;
        if (!prev_core || kPrenominal.count(detail::lower(*prev_core))) {
            capitalised = i + 1 >= words.size() || is_capitalised(words[i + 1]);
        } else {
            capitalised = !prev_core->empty() && detail::is_upper((*prev_core)[0]);
        }
        out.push_back((capitalised ? title(reading) : reading) + trail);
    }
    return to_utf8(detail::join(out, U" "));
}

std::string expand_acronyms(std::string_view text) {
    const std::vector<U32> words = detail::split_ws(from_utf8(text));
    std::vector<U32> cores;
    for (const U32& w : words) cores.push_back(split_edges(w).core);
    std::vector<U32> out;
    for (size_t i = 0; i < words.size(); ++i) {
        const Edges edges = split_edges(words[i]);
        auto it = acronyms().find(edges.core);
        const U32* repl = it == acronyms().end() ? nullptr : &it->second;
        if (kWordAcronyms.count(edges.core) && in_caps_text(cores, i)) repl = nullptr;
        out.push_back(edges.lead + (repl ? *repl : edges.core) + edges.trail);
    }
    return to_utf8(detail::join(out, U" "));
}

std::string expand_letter_digit_codes(std::string_view text, std::string_view lang) {
    const std::vector<U32> words = detail::split_ws(from_utf8(text));
    std::vector<U32> out;
    for (size_t i = 0; i < words.size(); ++i) {
        const Edges edges = split_edges(words[i]);
        const auto match = letter_digit_code(edges.core);
        const bool after_number = i > 0 && detail::all_digit(split_edges(words[i - 1]).core);
        if (!match || after_number) {
            out.push_back(words[i]);
            continue;
        }
        const std::string number = NumberParser::pronounce_number_word(
            to_utf8(match->second), std::nullopt, std::nullopt, std::string("masculine"), false, is_brazilian(lang));
        out.push_back(edges.lead + spell_letters(detail::upper(match->first)) + U" " + from_utf8(number) +
                      edges.trail);
    }
    return to_utf8(detail::join(out, U" "));
}

std::string normalize_orthography(std::string_view text, std::string_view lang) {
    std::string t = separate_unit_symbols(text);
    t = normalize_number_separators(t);
    t = split_number_ranges(t);
    t = expand_clock_times(t);
    t = expand_abbreviations(t);
    t = expand_regnal_numerals(t, lang);
    t = expand_acronyms(t);
    t = expand_letter_digit_codes(t, lang);
    return t;
}

}  // namespace tugaphone::text_normalization
