// A regular expression engine with the syntax and the matching rules of
// Python 3.12's re module, for the patterns a user supplies at run time
// (accent.Transform and a user RespellRule). The parser is a port of
// re._parser, so a pattern Python rejects is rejected here with the same
// exception class; the matcher backtracks in the order sre does, with sre's
// rules for empty iterations, IGNORECASE and must_advance.
//
// Not supported: \N{name} escapes (NotImplementedError), and the LOCALE flag,
// which Python also rejects for str patterns.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "detail.hpp"

namespace tugaphone::pyre {

// re's flag values.
enum Flag : int {
    IGNORECASE = 2,
    LOCALE = 4,
    MULTILINE = 8,
    DOTALL = 16,
    UNICODE = 32,
    VERBOSE = 64,
    ASCII = 256,
};

// re.error.
struct error : Error {
    explicit error(const std::string& message) : Error("error", message) {}
};

struct Program;

class Pattern {
public:
    // re.compile(pattern, flags).
    Pattern(detail::U32View pattern, int flags = 0);

    // Pattern.groups.
    int groups() const;
    // [(m.start(), m.end()) for m in re.finditer(pattern, s)], in code points.
    std::vector<std::pair<size_t, size_t>> finditer_spans(detail::U32View s) const;
    // Pattern.sub(repl, s, count) for a replacement template string.
    detail::U32 sub(detail::U32View repl, detail::U32View s, size_t count = 0) const;

private:
    std::shared_ptr<const Program> program_;
};

// re.escape(pattern).
detail::U32 escape(detail::U32View pattern);

}  // namespace tugaphone::pyre
