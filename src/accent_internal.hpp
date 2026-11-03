// Internal pieces of tugaphone.accent that the parity tests call directly.
#pragma once

#include <utility>
#include <vector>

#include "detail.hpp"

namespace tugaphone::accent::internal {

// _levenshtein over code points.
size_t levenshtein(detail::U32View a, detail::U32View b);
// _restore_case: the original word's leading capital on the respelling.
detail::U32 restore_case(detail::U32View original, detail::U32View respelled);
// The spans of _WORD_RE ([^\W\d_]+) in `text`.
std::vector<std::pair<size_t, size_t>> word_spans(detail::U32View text);

}  // namespace tugaphone::accent::internal
