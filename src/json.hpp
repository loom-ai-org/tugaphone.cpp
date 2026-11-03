// A JSON reader with the semantics of Python's json.loads: objects keep the
// first position of a key and the last value, NaN and ±Infinity are numbers,
// an integer keeps its digits, and a float out of the double range is ±inf
// or ±0.0 as Python's float() makes it.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tugaphone/errors.hpp"

namespace tugaphone::json {

struct JSONDecodeError : Error {
    explicit JSONDecodeError(const std::string& message) : Error("JSONDecodeError", message) {}
};

struct Value {
    enum Kind { Null, Bool, Int, Float, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0;   // Float, and Int as a double
    std::string text;    // String, and the digits of an Int
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    // The member under `key`, or nullptr.
    const Value* find(std::string_view key) const;
    const Value& at(std::string_view key) const;  // KeyError when absent
};

// Python's json.loads(text) for UTF-8 text.
Value loads(std::string_view text);

}  // namespace tugaphone::json
