// Internal helpers shared by the modules: the code-point layer that mirrors
// Python's str methods, number text, and file access.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tugaphone/errors.hpp"
#include "tugaphone/pynum.hpp"

namespace tugaphone::detail {

using U32 = std::u32string;
using U32View = std::u32string_view;

// UTF-8 conversion. Ill-formed input decodes as Python's
// bytes.decode("utf-8", "replace"): one U+FFFD per maximal subpart.
U32 from_utf8(std::string_view text);
std::string to_utf8(U32View text);

// One code point.
bool is_space(char32_t c);    // str.isspace()
bool is_word(char32_t c);     // re's \w: str.isalnum() or '_'
bool is_alnum(char32_t c);    // str.isalnum()
bool is_alpha(char32_t c);    // str.isalpha()
bool is_upper(char32_t c);    // str.isupper()
bool is_digit(char32_t c);    // str.isdigit()
bool is_decimal(char32_t c);  // str.isdecimal(), re's \d
int decimal_value(char32_t c);  // unicodedata.decimal(c, -1)

// Whole strings, as Python's str methods: true only for a non-empty string.
bool all_alpha(U32View s);
bool all_digit(U32View s);

// Full case mappings, Greek final sigma included.
U32 lower(U32View s);
U32 upper(U32View s);
U32 capitalize(U32View s);

// str.split() with no argument.
std::vector<U32> split_ws(U32View s);
// str.strip() with no argument.
U32 strip_ws(U32View s);
// str.strip(chars), str.lstrip(chars), str.rstrip(chars).
U32 strip(U32View s, U32View chars);
U32 lstrip(U32View s, U32View chars);
U32 rstrip(U32View s, U32View chars);
// sep.join(parts).
U32 join(const std::vector<U32>& parts, U32View sep);
// str.replace(old, new) and str.replace(old, new, count).
U32 replace(U32View s, U32View from, U32View to, long count = -1);

bool contains(U32View s, U32View part);  // `part in s`: "" is in every string
bool contains(U32View s, char32_t c);
bool starts_with(U32View s, U32View prefix);
bool ends_with(U32View s, U32View suffix);

// Python's text: decode once at the API edge, encode on the way out.
inline U32 u(std::string_view s) { return from_utf8(s); }
inline std::string s8(U32View s) { return to_utf8(s); }

// Files under the data directory.
std::string data_path(const std::string& relative);
// The bytes of a file; FileNotFoundError when it cannot be opened.
std::string read_file(const std::string& path);

// registry.resolve_lect(lang) from the Python caller that `call_site` names. The
// default warning filter prints a text once for each caller.
std::string resolve_lect_from(std::string_view lang, std::string_view call_site);

}  // namespace tugaphone::detail
