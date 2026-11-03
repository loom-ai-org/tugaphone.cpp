// Python's int and float, as far as the port needs them: an integer of any
// size, and the text forms that int(), float() and repr() read and write.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace tugaphone {

// A Python int of any size: a sign and the decimal digits of the magnitude.
// The digits have no leading zero, and zero is "0" with no sign.
struct BigInt {
    bool negative = false;
    std::string digits = "0";

    BigInt() = default;
    explicit BigInt(long long value);
    // Decimal digits with an optional leading '-'; std::invalid_argument otherwise.
    static BigInt from_string(std::string_view text);
    // The exact integer part of a finite double, as Python's int(x) gives it.
    static BigInt from_double(double value);

    bool is_zero() const { return digits == "0"; }
    // Python's str(n).
    std::string str() const { return negative ? "-" + digits : digits; }

    friend bool operator==(const BigInt& a, const BigInt& b) {
        return a.negative == b.negative && a.digits == b.digits;
    }
    friend bool operator!=(const BigInt& a, const BigInt& b) { return !(a == b); }
};

// A Python int or float.
using Number = std::variant<BigInt, double>;

// Python's int(text) for base 10, with the same leniency: surrounding Unicode
// whitespace, a sign, underscores between digits, and any Unicode decimal
// digit. nullopt where Python raises ValueError, which includes a value of more
// than 4300 digits.
std::optional<BigInt> py_int(std::string_view text);

// Python's float(text): the same leniency as py_int, a fraction and an
// exponent, and inf, infinity and nan in any case. A magnitude above the
// double range is ±inf and one below it is ±0.0. nullopt where Python raises
// ValueError.
std::optional<double> py_float(std::string_view text);

// Python's repr(x) of a float: the shortest digits that read back as x, in
// fixed notation for a decimal exponent in [-4, 16) and in exponent notation
// otherwise ('1e-05', '1e+16', '0.0001', '123.0').
std::string float_repr(double value);

}  // namespace tugaphone
