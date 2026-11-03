// Unicode normalization and the case rules of Python's re module, from tables
// generated out of the reference Python (tools/gen_unicode_norm_tables.py).
#pragma once

#include <vector>

#include "detail.hpp"
#include "nfd.hpp"

namespace tugaphone::detail {

// unicodedata.normalize("NFC", s). nfd and combining are in nfd.hpp.
U32 nfc(U32View s);

// The case rules re uses for IGNORECASE in Unicode mode.
char32_t sre_lower(char32_t c);   // _sre.unicode_tolower
char32_t sre_upper(char32_t c);   // the simple upper-case mapping
bool sre_iscased(char32_t c);     // _sre.unicode_iscased
// re._casefix._EXTRA_CASES[c]: the other lower-case forms that match c.
std::vector<char32_t> sre_case_fixes(char32_t c);

// str.isprintable() of one code point, which repr() of a str uses.
bool is_printable(char32_t c);
// str.isidentifier().
bool is_identifier(U32View s);
// repr(s) of a Python str.
std::string py_str_repr(U32View s);

}  // namespace tugaphone::detail
