// JSON writers for the parity records, byte for byte as Python's
// json.dumps(value, ensure_ascii=False, separators=(",", ":")) writes them.
#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <orthography2ipa/markup.hpp>

#include "../json_line.hpp"
#include "tugaphone/errors.hpp"
#include "tugaphone/pynum.hpp"

namespace parity {

using json_line::Value;

inline std::string J(const std::string& s) { return json_line::quote(s); }
inline std::string J(const char* s) { return json_line::quote(s); }
inline std::string J(bool b) { return b ? "true" : "false"; }
inline std::string J(int v) { return std::to_string(v); }
inline std::string J(long v) { return std::to_string(v); }
inline std::string J(long long v) { return std::to_string(v); }
inline std::string J(unsigned long v) { return std::to_string(v); }
inline std::string J(const tugaphone::BigInt& v) { return v.str(); }
// A float is written as its repr string.
inline std::string F(double v) { return json_line::quote(tugaphone::float_repr(v)); }
template <class T>
std::string J(const std::optional<T>& v) {
    return v ? J(*v) : "null";
}
template <class T>
std::string J(const std::vector<T>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + J(items[i]);
    return out + "]";
}
template <class T>
std::string J(const std::set<T>& items) {
    return J(std::vector<T>(items.begin(), items.end()));
}

// A JSON array of already written parts.
inline std::string list(const std::vector<std::string>& parts) {
    std::string out = "[";
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "," : "") + parts[i];
    return out + "]";
}

// The result of fn, or {"err":"<Python class>"} when it raises. A forced
// pronunciation that the spec cannot read raises orthography2ipa's MarkupError.
inline std::string guard(const std::function<std::string()>& fn) {
    try {
        return fn();
    } catch (const tugaphone::Error& e) {
        return "{\"err\":" + J(e.python_type()) + "}";
    } catch (const orthography2ipa::markup::MarkupError&) {
        return "{\"err\":\"MarkupError\"}";
    }
}

// The input part of a record: everything but the last item, as an open array.
inline std::string head(const Value& v) {
    std::string out = "[";
    for (size_t i = 0; i + 1 < v.size(); ++i) out += (i ? "," : "") + json_line::dump(v[i]);
    return out + (v.size() > 1 ? "," : "");
}

inline std::optional<std::string> opt_str(const Value& v) {
    if (v.is_null()) return std::nullopt;
    return v.str;
}

}  // namespace parity
