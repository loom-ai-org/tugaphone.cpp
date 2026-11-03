// ovos-number-parser's pronounce_number for Portuguese: the top-level
// dispatcher (__init__.py), the RomanceNumberExtractor reading
// (util.py) and the pt-PT and pt-BR vocabularies (numbers_pt.py).
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "detail.hpp"
#include "tugaphone/ovos_number_parser.hpp"

namespace tugaphone::ovos_number_parser {

namespace {

using Gender = GrammaticalGender;

// ── Number helpers ──────────────────────────────────────────────────────────

bool is_float(const Number& n) { return std::holds_alternative<double>(n); }

// A non-negative BigInt below 10^18 as an integer.
long long small(const BigInt& n) { return std::stoll(n.digits); }

bool below(const BigInt& n, long long limit) {
    return n.negative || (n.digits.size() <= 18 && small(n) < limit);
}

std::string strip_zeros(const std::string& digits) {
    const size_t first = digits.find_first_not_of('0');
    return first == std::string::npos ? "0" : digits.substr(first);
}

// n // 10^k and n % 10^k for n >= 10^k.
std::pair<BigInt, BigInt> divmod_pow10(const BigInt& n, size_t k) {
    BigInt count, remainder;
    count.digits = n.digits.substr(0, n.digits.size() - k);
    remainder.digits = strip_zeros(n.digits.substr(n.digits.size() - k));
    return {count, remainder};
}

// Python's s[start:stop] on a string of ASCII digits.
std::string py_slice(const std::string& s, long start, long stop) {
    const long n = static_cast<long>(s.size());
    auto clamp = [n](long i) { return i < 0 ? std::max(0L, i + n) : std::min(i, n); };
    const long b = clamp(start), e = clamp(stop);
    return b < e ? s.substr(static_cast<size_t>(b), static_cast<size_t>(e - b)) : std::string();
}

std::string rstrip_zeros(const std::string& s) {
    const size_t last = s.find_last_not_of('0');
    return last == std::string::npos ? std::string() : s.substr(0, last + 1);
}

// format(Decimal(repr(x)), "f") for a non-negative finite x whose repr uses
// exponent notation.
std::string decimal_fixed(const std::string& repr) {
    const size_t e = repr.find_first_of("eE");
    std::string digits;
    for (char c : repr.substr(0, e)) {
        if (c != '.') digits.push_back(c);
    }
    const size_t dot = repr.find('.');
    const long frac_len = dot == std::string::npos || dot > e ? 0 : static_cast<long>(e - dot - 1);
    const long exponent = std::stol(repr.substr(e + 1)) - frac_len;
    const long len = static_cast<long>(digits.size());
    if (exponent >= 0) return digits + std::string(static_cast<size_t>(exponent), '0');
    if (len > -exponent) {
        const size_t at = static_cast<size_t>(len + exponent);
        return digits.substr(0, at) + "." + digits.substr(at);
    }
    return "0." + std::string(static_cast<size_t>(-exponent - len), '0') + digits;
}

// ── Vocabularies (numbers_pt.py) ────────────────────────────────────────────

struct Vocab {
    std::map<int, std::string> units, tens, hundreds;
    // Scale words by the exponent of their power of ten.
    std::map<int, std::string> short_scale, long_scale;
    std::map<int, std::string> ordinal_units, ordinal_tens, ordinal_hundreds;
    std::map<int, std::string> ordinal_short_scale, ordinal_long_scale;
    Scale default_scale;
};

const std::map<int, std::string> kUnits = {{0, "zero"}, {1, "um"},     {2, "dois"}, {3, "três"},
                                           {4, "quatro"}, {5, "cinco"}, {6, "seis"}, {7, "sete"},
                                           {8, "oito"},   {9, "nove"}};
const std::map<int, std::string> kTensPt = {
    {10, "dez"},      {11, "onze"},      {12, "doze"},    {13, "treze"},   {14, "catorze"},
    {15, "quinze"},   {16, "dezasseis"}, {17, "dezassete"}, {18, "dezoito"}, {19, "dezanove"},
    {20, "vinte"},    {30, "trinta"},    {40, "quarenta"}, {50, "cinquenta"}, {60, "sessenta"},
    {70, "setenta"},  {80, "oitenta"},   {90, "noventa"}};
const std::map<int, std::string> kHundreds = {
    {100, "cem"},          {200, "duzentos"},   {300, "trezentos"}, {400, "quatrocentos"}, {500, "quinhentos"},
    {600, "seiscentos"},   {700, "setecentos"}, {800, "oitocentos"}, {900, "novecentos"}};
const std::vector<std::pair<int, const char*>> kShortScaleStems = {
    {303, "centili"},      {63, "vigintili"},     {60, "novendecili"}, {57, "octodecili"},
    {54, "septendecili"},  {51, "sedecili"},      {48, "quindecili"},  {45, "quadriodecili"},
    {42, "tredecili"},     {39, "dudecili"},      {36, "undecili"},    {33, "decili"},
    {30, "nonili"},        {27, "octili"},        {24, "septili"},     {21, "sextili"},
    {18, "quintili"},      {15, "quatrili"},      {12, "trili"},       {9, "bili"}};
const std::vector<std::pair<int, const char*>> kLongScaleStems = {
    {600, "centili"},      {120, "vigintili"},    {114, "novendecili"}, {108, "octodecili"},
    {102, "septendecili"}, {96, "sedecili"},      {90, "quindecili"},  {84, "quadriodecili"},
    {78, "tredecili"},     {72, "dudecili"},      {66, "undecili"},    {60, "decili"},
    {54, "nonili"},        {48, "octili"},        {42, "septili"},     {36, "sextili"},
    {30, "quintili"},      {24, "quatrili"},      {18, "trili"},       {12, "bili"}};
const std::map<int, std::string> kOrdinalUnits = {{1, "primeiro"}, {2, "segundo"}, {3, "terceiro"},
                                                  {4, "quarto"},   {5, "quinto"},  {6, "sexto"},
                                                  {7, "sétimo"},   {8, "oitavo"},  {9, "nono"}};
const std::map<int, std::string> kOrdinalTens = {
    {10, "décimo"},      {20, "vigésimo"},    {30, "trigésimo"},   {40, "quadragésimo"}, {50, "quinquagésimo"},
    {60, "sexagésimo"},  {70, "septuagésimo"}, {80, "octogésimo"}, {90, "nonagésimo"}};
const std::map<int, std::string> kOrdinalHundreds = {
    {100, "centésimo"},      {200, "ducentésimo"},    {300, "trecentésimo"},
    {400, "quadrigentésimo"}, {500, "quingentésimo"}, {600, "sexcentésimo"},
    {700, "septingentésimo"}, {800, "octingentésimo"}, {900, "noningentésimo"}};
const std::map<int, std::string> kOrdinalShortScale = {
    {21, "sextilionésimo"}, {18, "quintilionésimo"}, {15, "quatrilionésimo"}, {12, "trilionésimo"},
    {9, "bilionésimo"},     {6, "milionésimo"},      {3, "milésimo"}};
const std::map<int, std::string> kOrdinalLongScale = {
    {36, "sextilionésimo"}, {30, "quintilionésimo"}, {24, "quatrilionésimo"}, {18, "trilionésimo"},
    {12, "bilionésimo"},    {6, "milionésimo"},      {3, "milésimo"}};

// pt-PT spells the scale words "-ilião" and pt-BR "-ilhão"; both share
// "milhão" and "mil".
std::map<int, std::string> scale_words(const std::vector<std::pair<int, const char*>>& stems, const char* ending) {
    std::map<int, std::string> out;
    for (const auto& [exponent, stem] : stems) out[exponent] = std::string(stem) + ending;
    out[6] = "milhão";
    out[3] = "mil";
    return out;
}

Vocab make_vocab(bool brazilian) {
    Vocab v;
    v.units = kUnits;
    v.tens = kTensPt;
    if (brazilian) {
        v.tens[16] = "dezesseis";
        v.tens[17] = "dezessete";
        v.tens[19] = "dezenove";
    }
    v.hundreds = kHundreds;
    const char* ending = brazilian ? "hão" : "ão";
    v.short_scale = scale_words(kShortScaleStems, ending);
    v.long_scale = scale_words(kLongScaleStems, ending);
    v.ordinal_units = kOrdinalUnits;
    v.ordinal_tens = kOrdinalTens;
    v.ordinal_hundreds = kOrdinalHundreds;
    v.ordinal_short_scale = kOrdinalShortScale;
    v.ordinal_long_scale = kOrdinalLongScale;
    v.default_scale = brazilian ? Scale::SHORT : Scale::LONG;
    return v;
}

const Vocab& vocab(bool brazilian) {
    static const Vocab pt = make_vocab(false);
    static const Vocab br = make_vocab(true);
    return brazilian ? br : pt;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The word without its last n bytes.
std::string drop_last(const std::string& s, size_t n) { return s.substr(0, s.size() - n); }

// numbers_pt.swap_gender_pt.
std::string swap_gender(const std::string& word, Gender gender) {
    static const char* const kInvariant[] = {"zero", "quatro", "cinco", "oito",    "cem",  "cento",
                                             "mil",  "milhão", "bilhão", "trilhão", "conto"};
    for (const char* w : kInvariant) {
        if (word == w) return word;
    }
    if (word == "dois" && gender == Gender::FEMININE) return "duas";
    if (word == "duas" && gender == Gender::MASCULINE) return "dois";
    if (gender == Gender::FEMININE && ends_with(word, "o")) return drop_last(word, 1) + "a";
    if (gender == Gender::MASCULINE && ends_with(word, "ma")) return drop_last(word, 1);
    if (gender == Gender::MASCULINE && ends_with(word, "a")) return drop_last(word, 1) + "o";
    if (gender == Gender::FEMININE && ends_with(word, "os")) return drop_last(word, 2) + "as";
    if (gender == Gender::MASCULINE && ends_with(word, "as")) return drop_last(word, 2) + "os";
    if (gender == Gender::FEMININE && ends_with(word, "m")) return word + "a";
    return word;
}

// numbers_pt.pluralize_pt.
std::string pluralize(const std::string& word) {
    if (ends_with(word, "ão")) return drop_last(word, std::string("ão").size()) + "ões";
    if (!ends_with(word, "s")) return word + "s";
    return word;
}

// GENDERED_SPELLINGS: only the feminine of one and two differ.
const char* gendered_spelling(Gender gender, long long n) {
    if (gender != Gender::FEMININE) return nullptr;
    if (n == 1) return "uma";
    if (n == 2) return "duas";
    return nullptr;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + parts[i];
    return out;
}

// The largest scale exponent k with n >= 10^k, for n >= 1000.
std::pair<int, std::string> largest_scale(const std::map<int, std::string>& scales, const BigInt& n) {
    std::pair<int, std::string> best{-1, ""};
    for (const auto& [k, word] : scales) {
        if (n.digits.size() > static_cast<size_t>(k)) best = {k, word};
    }
    return best;
}

// ── RomanceNumberExtractor ──────────────────────────────────────────────────

class Romance {
public:
    explicit Romance(bool brazilian) : v_(vocab(brazilian)) {}

    std::string pronounce_number(const Number& number, int places, std::optional<Scale> scale_opt, bool ordinals,
                                 DigitPronunciation digits, Gender gender, bool force_hundreds_join = false,
                                 bool under_higher_scale = false) const {
        const Scale scale = scale_opt ? *scale_opt : v_.default_scale;
        if (is_float(number) && !std::isfinite(std::get<double>(number))) return float_repr(std::get<double>(number));
        if (ordinals) return pronounce_ordinal(number, gender, scale);

        if (negative(number)) {
            return "menos " + pronounce_number(absolute(number), places, scale, false, digits, gender);
        }

        std::string num_str;
        if (is_float(number)) {
            num_str = float_repr(std::get<double>(number));
            if (num_str.find_first_of("eE") != std::string::npos) num_str = decimal_fixed(num_str);
        } else {
            num_str = std::get<BigInt>(number).str();
        }

        if (num_str.find('.') != std::string::npos) {
            const BigInt integer_part = to_int(number);
            std::string decimal_part = rstrip_zeros(num_str.substr(num_str.find('.') + 1));
            if (!decimal_part.empty() && strip_zeros(decimal_part) == "0") {
                return pronounce_number(integer_part, places, scale, false, digits, gender);
            }
            const std::string int_pronunciation = pronounce_number(integer_part, places, scale, false, digits, gender);
            std::vector<std::string> parts;
            if (!decimal_part.empty()) {
                if (digits == DigitPronunciation::FULL_NUMBER) {
                    const std::string no_zeros = decimal_part.substr(decimal_part.find_first_not_of('0'));
                    const long n_zeros = static_cast<long>(decimal_part.size() - no_zeros.size());
                    for (long i = 0; i < n_zeros; ++i) parts.push_back(v_.units.at(0));
                    if (n_zeros >= places) {
                        int last_digit = decimal_part[static_cast<size_t>(n_zeros)] - '0';
                        const int after = static_cast<long>(decimal_part.size()) > n_zeros + 1
                                              ? decimal_part[static_cast<size_t>(n_zeros + 1)] - '0'
                                              : 0;
                        if (after >= 5) last_digit += 1;
                        parts.push_back(up_to_999(last_digit, gender));
                    } else {
                        if (static_cast<long>(decimal_part.size()) > places) {
                            int last_digit = decimal_part[static_cast<size_t>(places - 1)] - '0';
                            const int after = decimal_part[static_cast<size_t>(places)] - '0';
                            if (after >= 5) last_digit += 1;
                            decimal_part = py_slice(decimal_part, 0, places - 1) + std::to_string(last_digit);
                        }
                        parts.push_back(pronounce_number(BigInt::from_string(decimal_part), 5, std::nullopt, false,
                                                         DigitPronunciation::FULL_NUMBER, gender));
                    }
                } else {
                    for (char d : decimal_part) {
                        parts.push_back(pronounce_number(BigInt(d - '0'), 5, std::nullopt, false,
                                                         DigitPronunciation::FULL_NUMBER, gender));
                    }
                }
            }
            const std::string decimal_pronunciation = parts.empty() ? v_.units.at(0) : join(parts);
            return int_pronunciation + " vírgula " + decimal_pronunciation;
        }

        const BigInt n = to_int(number);
        if (below(n, 1000)) {
            return up_to_999(small(n), gender, force_hundreds_join, !under_higher_scale);
        }
        const auto& scales = scale == Scale::SHORT ? v_.short_scale : v_.long_scale;
        const auto [k, word] = largest_scale(scales, n);
        const auto [count, remainder] = divmod_pow10(n, static_cast<size_t>(k));
        const bool count_is_one = count.digits == "1";
        const bool is_singular = count_is_one || k == 3;  // NO_PLURAL = [1000]
        const std::string scale_word = is_singular ? word : pluralize(word);
        const bool nested_under_higher = under_higher_scale || k >= 6;
        std::string count_str;
        if (count_is_one && k == 3) {  // NO_PREV_UNIT = [100, 1000]
            count_str = scale_word;
        } else {
            count_str = pronounce_number(count, places, scale, false, DigitPronunciation::FULL_NUMBER,
                                         Gender::MASCULINE, false, true) +
                        " " + scale_word;
        }
        if (remainder.is_zero()) return count_str;
        const bool remainder_force = k == 3 && !under_higher_scale;
        const std::string remainder_str = pronounce_number(remainder, places, scale, false,
                                                           DigitPronunciation::FULL_NUMBER, Gender::MASCULINE,
                                                           remainder_force, nested_under_higher);
        // JOINER_ON_THOUSANDS: join a final group below 100 or of whole hundreds.
        if (below(remainder, 100) || (below(remainder, 1000) && small(remainder) % 100 == 0)) {
            return count_str + " e " + remainder_str;
        }
        return count_str + " " + remainder_str;
    }

    std::string pronounce_ordinal(const Number& number, Gender gender, Scale scale) const {
        if (is_zero(number)) return v_.units.at(0);
        if (negative(number)) return "menos " + pronounce_ordinal(absolute(number), gender, scale);
        const BigInt n = to_int(number);
        if (below(n, 1000)) return ordinal_up_to_999(small(n), gender);
        const auto& scales = scale == Scale::SHORT ? v_.ordinal_short_scale : v_.ordinal_long_scale;
        const auto [k, word] = largest_scale(scales, n);
        const auto [count, remainder] = divmod_pow10(n, static_cast<size_t>(k));
        std::string count_str;
        if (count.digits == "1") {
            count_str = swap_gender(word, gender);
        } else {
            count_str = pronounce_number(count, 5, scale, false, DigitPronunciation::FULL_NUMBER, Gender::MASCULINE) +
                        " " + swap_gender(word, gender);
        }
        if (remainder.is_zero()) return count_str;
        return count_str + " " + pronounce_ordinal(remainder, gender, scale);
    }

    std::string up_to_999(long long n, Gender gender, bool force_hundreds_join = false,
                          bool terminal_group = true) const {
        // JOINER_ON_HUNDREDS is on and JOINER_ON_HUNDRED_PARTICLE is off for
        // Portuguese, so neither flag changes the reading.
        (void)force_hundreds_join;
        (void)terminal_group;
        if (n < 0 || n > 999) throw ValueError("Number must be between 0 and 999.");
        auto gendered = [&](long long val, const std::string& base) -> std::string {
            if (const char* special = gendered_spelling(gender, val)) return special;
            if (gender == Gender::MASCULINE) return base;
            return swap_gender(base, gender);
        };
        const int i = static_cast<int>(n);
        if (const char* special = gendered_spelling(gender, n)) return special;
        if (v_.units.count(i)) return gendered(n, v_.units.at(i));
        if (v_.tens.count(i)) return gendered(n, v_.tens.at(i));
        if (v_.hundreds.count(i)) return gendered(n, v_.hundreds.at(i));

        std::vector<std::string> parts;
        int rest = i;
        if (rest >= 100) {
            const int hundred = rest / 100 * 100;
            parts.push_back(hundred == 100 ? std::string("cento") : gendered(hundred, v_.hundreds.at(hundred)));
            rest %= 100;
            // JOINER_ON_HUNDREDS is on, so the conjunction follows whether or
            // not the caller forces it.
            if (rest > 0) parts.push_back("e");
        }
        if (rest > 0) {
            if (v_.tens.count(rest)) {
                parts.push_back(gendered(rest, v_.tens.at(rest)));
            } else if (rest < 20) {
                parts.push_back(gendered(rest, v_.units.at(rest)));
            } else {
                const int ten = rest / 10 * 10;
                const int unit = rest % 10;
                parts.push_back(gendered(ten, v_.tens.at(ten)));
                if (unit > 0) {
                    parts.push_back("e");  // JOINER_ON_TWENTYS
                    parts.push_back(gendered(unit, v_.units.at(unit)));
                }
            }
        }
        return join(parts);
    }

    std::string ordinal_up_to_999(long long n, Gender gender) const {
        if (n < 0 || n > 999) throw ValueError("Number must be between 0 and 999.");
        if (n == 0) return v_.units.at(0);
        std::vector<std::string> parts;
        int rest = static_cast<int>(n);
        if (rest >= 100) {
            const int hundred = rest / 100 * 100;
            auto it = v_.ordinal_hundreds.find(hundred);
            if (it != v_.ordinal_hundreds.end()) parts.push_back(swap_gender(it->second, gender));
            rest %= 100;
        }
        if (rest > 0) {
            if (rest % 10 == 0) {
                parts.push_back(swap_gender(v_.ordinal_tens.at(rest), gender));
            } else if (rest < 10) {
                parts.push_back(swap_gender(v_.ordinal_units.at(rest), gender));
            } else if (rest < 20) {
                parts.push_back(swap_gender(v_.ordinal_tens.at(10), gender) + " " +
                                swap_gender(v_.ordinal_units.at(rest - 10), gender));
            } else {
                parts.push_back(swap_gender(v_.ordinal_tens.at(rest / 10 * 10), gender) + " " +
                                swap_gender(v_.ordinal_units.at(rest % 10), gender));
            }
        }
        return swap_gender(join(parts), gender);
    }

private:
    static bool negative(const Number& n) {
        return is_float(n) ? std::get<double>(n) < 0 : std::get<BigInt>(n).negative;
    }
    static bool is_zero(const Number& n) {
        return is_float(n) ? std::get<double>(n) == 0 : std::get<BigInt>(n).is_zero();
    }
    static Number absolute(const Number& n) {
        if (is_float(n)) return std::fabs(std::get<double>(n));
        BigInt out = std::get<BigInt>(n);
        out.negative = false;
        return out;
    }
    static BigInt to_int(const Number& n) {
        return is_float(n) ? BigInt::from_double(std::get<double>(n)) : std::get<BigInt>(n);
    }

    const Vocab& v_;
};

// ── The top-level dispatcher (__init__.py) ──────────────────────────────────

std::string base_lang(std::string_view lang) {
    std::string out;
    for (char32_t c : detail::lower(detail::from_utf8(lang))) {
        if (c == '-') break;
        out += detail::to_utf8(std::u32string(1, c));
    }
    return out;
}

bool holds_br(std::string_view lang) {
    return detail::contains(detail::lower(detail::from_utf8(lang)), U"br");
}

Scale resolve_scale(std::string_view lang, std::optional<Scale> scale, std::optional<bool> short_scale) {
    if (scale) return *scale;
    if (short_scale) return *short_scale ? Scale::SHORT : Scale::LONG;
    // _canonical_scale: Portuguese by region, the long-scale languages, else short.
    static const char* const kLong[] = {"de", "fr", "es", "it", "nl", "pl", "sv", "da", "nb", "nn",
                                        "no", "gl", "ca", "eu", "cs", "sl", "sk", "hr", "hu", "fi",
                                        "et", "ro", "ast", "oc", "an", "mwl", "fy"};
    const std::string lang2 = base_lang(lang);
    if (lang2 == "pt") return holds_br(lang) ? Scale::SHORT : Scale::LONG;
    for (const char* l : kLong) {
        if (lang2 == l) return Scale::LONG;
    }
    return Scale::SHORT;
}

// _round_for_speech: round a float to `places` decimals, half away from zero.
Number round_for_speech(const Number& number, int places) {
    if (!is_float(number)) return number;
    const double x = std::get<double>(number);
    if (!std::isfinite(x)) return number;
    if (x == 0) return 0.0;
    if (std::trunc(x) == x) return number;

    // Decimal(str(x)): sign, coefficient digits and exponent.
    std::string repr = float_repr(x);
    const bool neg = repr[0] == '-';
    if (neg) repr.erase(0, 1);
    std::string coefficient;
    long exponent = 0;
    const size_t e = repr.find('e');
    const std::string mantissa = repr.substr(0, e);
    const size_t dot = mantissa.find('.');
    for (char c : mantissa) {
        if (c != '.') coefficient.push_back(c);
    }
    if (dot != std::string::npos) exponent -= static_cast<long>(mantissa.size() - dot - 1);
    if (e != std::string::npos) exponent += std::stol(repr.substr(e + 1));

    // quantize(Decimal(1).scaleb(-places), ROUND_HALF_UP) in the default
    // context: 28 digits of precision; a wider result is InvalidOperation,
    // which leaves the value as it is.
    const long target = -static_cast<long>(places);
    if (target > 999999 || target < -1000026) return number;
    std::string rounded;
    if (exponent >= target) {
        rounded = coefficient + std::string(static_cast<size_t>(exponent - target), '0');
    } else {
        const long drop = target - exponent;
        if (drop > static_cast<long>(coefficient.size())) {
            rounded = "0";
        } else {
            const size_t keep = coefficient.size() - static_cast<size_t>(drop);
            rounded = keep == 0 ? std::string("0") : coefficient.substr(0, keep);
            if (coefficient[keep] >= '5') {
                // add one to the kept digits
                size_t i = rounded.size();
                while (i > 0 && rounded[i - 1] == '9') rounded[--i] = '0';
                if (i == 0) {
                    rounded.insert(rounded.begin(), '1');
                } else {
                    ++rounded[i - 1];
                }
                if (keep == 0) rounded = "1";
            }
        }
    }
    rounded = strip_zeros(rounded);
    if (rounded.size() > 28) return number;
    if (rounded == "0") return number;

    // rounded == rounded.to_integral_value()
    if (target >= 0) {
        BigInt out;
        out.digits = rounded + std::string(static_cast<size_t>(target), '0');
        out.negative = neg;
        return out;
    }
    const size_t frac = static_cast<size_t>(-target);
    std::string padded = rounded.size() <= frac ? std::string(frac + 1 - rounded.size(), '0') + rounded : rounded;
    const std::string int_part = padded.substr(0, padded.size() - frac);
    const std::string frac_part = padded.substr(padded.size() - frac);
    if (frac_part.find_first_not_of('0') == std::string::npos) {
        BigInt out;
        out.digits = strip_zeros(int_part);
        out.negative = neg && !out.is_zero();
        return out;
    }
    const auto value = py_float((neg ? "-" : "") + int_part + "." + frac_part);
    return *value;
}

std::string pronounce_infinity(double x, std::string_view lang) {
    static const std::map<std::string, std::pair<const char*, const char*>> kWords = {
        {"en", {"infinity", "negative infinity"}}, {"es", {"infinito", "infinito negativo"}},
        {"pt", {"infinito", "infinito negativo"}}, {"it", {"infinito", "meno infinito"}},
        {"fr", {"infini", "moins infini"}},       {"de", {"unendlich", "minus unendlich"}}};
    auto it = kWords.find(base_lang(lang));
    const auto& words = it == kWords.end() ? kWords.at("en") : it->second;
    return x < 0 ? words.second : words.first;
}

const std::map<std::string, std::string>& scientific_words() {
    static const std::map<std::string, std::string> kWords = {
        {"en", "times ten to the power of"}, {"es", "por diez elevado a"}, {"pt", "vezes dez elevado a"},
        {"it", "per dieci alla"},           {"fr", "fois dix puissance"}, {"de", "mal zehn hoch"},
        {"nl", "maal tien tot de macht"},   {"ca", "per deu elevat a"},   {"gl", "veces dez elevado a"},
        {"ro", "ori zece la puterea"},      {"da", "gange ti i"},         {"sv", "gånger tio upphöjt till"}};
    return kWords;
}

// _has_padded_scale_words: two identical words side by side.
bool has_padded_scale_words(const std::string& spoken) {
    const auto words = detail::split_ws(detail::from_utf8(spoken));
    for (size_t i = 0; i + 1 < words.size(); ++i) {
        if (words[i] == words[i + 1]) return true;
    }
    return false;
}

// _has_unread_digits: a run of seven or more decimal digits.
bool has_unread_digits(const std::string& spoken) {
    size_t run = 0;
    for (char32_t c : detail::from_utf8(spoken)) {
        run = detail::is_decimal(c) ? run + 1 : 0;
        if (run >= 7) return true;
    }
    return false;
}

std::string pronounce_bignum_fallback(const Number& number, std::string_view lang, int places) {
    BigInt magnitude = is_float(number) ? BigInt::from_double(std::get<double>(number)) : std::get<BigInt>(number);
    magnitude.negative = false;
    const std::string& digits = magnitude.digits;
    const long exponent = static_cast<long>(digits.size()) - 1;
    const std::string tail = rstrip_zeros(py_slice(digits, 1, 1L + places));
    double mantissa = *py_float(digits.substr(0, 1) + (tail.empty() ? "" : "." + tail));
    const bool neg = is_float(number) ? std::get<double>(number) < 0 : std::get<BigInt>(number).negative;
    if (neg) mantissa = -mantissa;
    const std::string mantissa_words = pronounce_number(mantissa, lang, places);
    const std::string exponent_words = pronounce_number(BigInt(exponent), lang);
    auto it = scientific_words().find(base_lang(lang));
    const std::string& connective = it == scientific_words().end() ? scientific_words().at("en") : it->second;
    return mantissa_words + " " + connective + " " + exponent_words;
}

}  // namespace

std::string pronounce_number(const Number& number, std::string_view lang, int places, std::optional<bool> short_scale,
                             bool scientific, bool ordinals, std::optional<DigitPronunciation> digits,
                             GrammaticalGender gender, std::optional<Scale> scale) {
    const Scale resolved = resolve_scale(lang, scale, short_scale);
    if (is_float(number)) {
        const double x = std::get<double>(number);
        if (std::isnan(x)) throw ValueError("cannot pronounce NaN (not a number)");
        if (std::isinf(x)) return pronounce_infinity(x, lang);
    }
    const std::string lang2 = base_lang(lang);
    if (!digits) {
        digits = (lang2 == "es" || lang2 == "ca" || lang2 == "fr") ? DigitPronunciation::DIGIT_BY_DIGIT
                                                                   : DigitPronunciation::FULL_NUMBER;
    }
    const Number rounded = round_for_speech(number, places);

    if (!detail::starts_with(detail::from_utf8(lang), U"pt")) {
        throw NotImplementedError("pronounce_number is ported for Portuguese only: " + std::string(lang));
    }
    const Romance romance(holds_br(lang));
    const std::string spoken = romance.pronounce_number(rounded, places, resolved, ordinals, *digits, gender);

    const bool large = is_float(rounded) ? std::fabs(std::get<double>(rounded)) >= 1e15
                                         : std::get<BigInt>(rounded).digits.size() >= 16;
    const bool integral = !is_float(rounded) || std::trunc(std::get<double>(rounded)) == std::get<double>(rounded);
    if (large && !scientific && integral) {
        if (has_padded_scale_words(spoken)) return pronounce_bignum_fallback(rounded, lang, places);
        if (has_unread_digits(spoken) && scientific_words().count(lang2)) {
            return pronounce_bignum_fallback(rounded, lang, places);
        }
    }
    return spoken;
}

}  // namespace tugaphone::ovos_number_parser
