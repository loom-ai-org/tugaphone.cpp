// Internal pieces of tugaphone.langdetect that the parity tests call directly.
#pragma once

#include <vector>

#include "detail.hpp"

namespace tugaphone::langdetect::internal {

using detail::U32;
using detail::U32View;

// _normalize: NFC, lower case, then only the alphabetic code points.
U32 normalize(U32View word);
// _wrap: the word's code points between the \x02 and \x03 sentinels.
std::vector<U32> wrap(U32View word);

}  // namespace tugaphone::langdetect::internal
