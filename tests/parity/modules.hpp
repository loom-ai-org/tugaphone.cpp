// One parity module per Python module: tests/parity/<module>.cpp defines
// parity::record_<module>, which recomputes one golden record in C++, and
// tests/goldens/<module>.jsonl.gz holds the Python records.
#pragma once

#include <string>

#include "../json_line.hpp"

// One line per module.
#define TUGAPHONE_PARITY_MODULES(X) \
    X(registry)                     \
    X(dialects)                     \
    X(text_normalization)           \
    X(number_utils)                 \
    X(version)                      \
    X(markov)                       \
    X(langdetect)                   \
    X(codeswitch)                   \
    X(accent)                       \
    X(tokenizer)

namespace parity {
#define TUGAPHONE_PARITY_DECLARE(name) std::string record_##name(const json_line::Value& line);
TUGAPHONE_PARITY_MODULES(TUGAPHONE_PARITY_DECLARE)
#undef TUGAPHONE_PARITY_DECLARE
}  // namespace parity
