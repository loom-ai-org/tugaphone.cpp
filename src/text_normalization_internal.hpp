// Code-point entry points of tugaphone.text_normalization for the other
// modules of the port.
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "detail.hpp"

namespace tugaphone::text_normalization::internal {

// stays_portuguese on words that are already code points.
bool stays_portuguese(const std::vector<detail::U32>& words, std::size_t i,
                      const std::function<bool(const detail::U32&)>& is_foreign);

}  // namespace tugaphone::text_normalization::internal
