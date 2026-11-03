#include "json.hpp"

#include <charconv>
#include <cmath>
#include <limits>

#include "detail.hpp"

namespace tugaphone::json {

namespace {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Value document() {
        skip_ws();
        Value v = value();
        skip_ws();
        if (p_ != s_.size()) fail("Extra data");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw JSONDecodeError(what + " at byte " + std::to_string(p_));
    }
    char peek() const { return p_ < s_.size() ? s_[p_] : '\0'; }
    bool eat(std::string_view word) {
        if (s_.substr(p_, word.size()) != word) return false;
        p_ += word.size();
        return true;
    }
    void skip_ws() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
    }

    Value value() {
        Value v;
        const char c = peek();
        if (c == '"') {
            v.kind = Value::String;
            v.text = string();
        } else if (c == '{') {
            object(v);
        } else if (c == '[') {
            array(v);
        } else if (eat("null")) {
        } else if (eat("true")) {
            v.kind = Value::Bool;
            v.boolean = true;
        } else if (eat("false")) {
            v.kind = Value::Bool;
        } else if (eat("NaN")) {
            v.kind = Value::Float;
            v.number = std::numeric_limits<double>::quiet_NaN();
        } else if (eat("Infinity")) {
            v.kind = Value::Float;
            v.number = std::numeric_limits<double>::infinity();
        } else if (eat("-Infinity")) {
            v.kind = Value::Float;
            v.number = -std::numeric_limits<double>::infinity();
        } else {
            number(v);
        }
        return v;
    }

    void object(Value& v) {
        v.kind = Value::Object;
        ++p_;
        skip_ws();
        if (peek() == '}') {
            ++p_;
            return;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') fail("Expecting property name enclosed in double quotes");
            std::string key = string();
            skip_ws();
            if (peek() != ':') fail("Expecting ':' delimiter");
            ++p_;
            skip_ws();
            Value item = value();
            bool replaced = false;
            for (auto& member : v.members) {
                if (member.first == key) {
                    member.second = std::move(item);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) v.members.emplace_back(std::move(key), std::move(item));
            skip_ws();
            if (peek() == ',') {
                ++p_;
                continue;
            }
            if (peek() == '}') {
                ++p_;
                return;
            }
            fail("Expecting ',' delimiter");
        }
    }

    void array(Value& v) {
        v.kind = Value::Array;
        ++p_;
        skip_ws();
        if (peek() == ']') {
            ++p_;
            return;
        }
        while (true) {
            skip_ws();
            v.items.push_back(value());
            skip_ws();
            if (peek() == ',') {
                ++p_;
                continue;
            }
            if (peek() == ']') {
                ++p_;
                return;
            }
            fail("Expecting ',' delimiter");
        }
    }

    // NUMBER_RE: -?(0|[1-9]\d*)(\.\d+)?([eE][-+]?\d+)?
    void number(Value& v) {
        const size_t start = p_;
        auto digit = [&] { return peek() >= '0' && peek() <= '9'; };
        if (peek() == '-') ++p_;
        if (peek() == '0') {
            ++p_;
        } else if (digit()) {
            while (digit()) ++p_;
        } else {
            p_ = start;
            fail("Expecting value");
        }
        bool is_float = false;
        if (peek() == '.' && p_ + 1 < s_.size() && s_[p_ + 1] >= '0' && s_[p_ + 1] <= '9') {
            is_float = true;
            ++p_;
            while (digit()) ++p_;
        }
        if (peek() == 'e' || peek() == 'E') {
            size_t q = p_ + 1;
            if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) ++q;
            if (q < s_.size() && s_[q] >= '0' && s_[q] <= '9') {
                is_float = true;
                p_ = q;
                while (digit()) ++p_;
            }
        }
        const std::string_view text = s_.substr(start, p_ - start);
        const auto value = py_float(text);
        v.number = *value;
        if (is_float) {
            v.kind = Value::Float;
        } else {
            v.kind = Value::Int;
            v.text = std::string(text);
        }
    }

    unsigned hex4() {
        if (p_ + 4 > s_.size()) fail("Invalid \\uXXXX escape");
        unsigned value = 0;
        for (size_t k = 0; k < 4; ++k) {
            const char c = s_[p_ + k];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("Invalid \\uXXXX escape");
            }
        }
        p_ += 4;
        return value;
    }

    std::string string() {
        ++p_;  // the opening quote
        std::u32string out;
        while (true) {
            if (p_ >= s_.size()) fail("Unterminated string");
            const char c = s_[p_];
            if (c == '"') {
                ++p_;
                return detail::to_utf8(out);
            }
            if (static_cast<unsigned char>(c) < 0x20) fail("Invalid control character");
            if (c != '\\') {
                // one UTF-8 sequence
                size_t len = 1;
                const unsigned char b = static_cast<unsigned char>(c);
                if (b >= 0xF0) {
                    len = 4;
                } else if (b >= 0xE0) {
                    len = 3;
                } else if (b >= 0xC0) {
                    len = 2;
                }
                out += detail::from_utf8(s_.substr(p_, len));
                p_ += len;
                continue;
            }
            ++p_;
            const char e = peek();
            ++p_;
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.substr(p_, 2) == "\\u") {
                        const size_t save = p_;
                        p_ += 2;
                        const unsigned low = hex4();
                        if (low >= 0xDC00 && low < 0xE000) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            p_ = save;
                        }
                    }
                    out.push_back(static_cast<char32_t>(cp));
                    break;
                }
                default:
                    fail("Invalid \\escape");
            }
        }
    }

    std::string_view s_;
    size_t p_ = 0;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    for (const auto& member : members) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

const Value& Value::at(std::string_view key) const {
    const Value* v = find(key);
    if (v == nullptr) throw KeyError(std::string(key));
    return *v;
}

Value loads(std::string_view text) { return Parser(text).document(); }

}  // namespace tugaphone::json
