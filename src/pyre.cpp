#include "pyre.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>

#include "unicode_norm.hpp"

namespace tugaphone::pyre {

using detail::U32;
using detail::U32View;

namespace impl {

constexpr uint64_t kMaxRepeat = 4294967295ULL;  // _sre.MAXREPEAT
constexpr uint64_t kMaxGroups = 1073741823ULL;  // _sre.MAXGROUPS
constexpr uint64_t kMaxCode = 4294967295ULL;    // re._compiler.MAXCODE
constexpr uint64_t kInf = std::numeric_limits<uint64_t>::max();  // MAXWIDTH, saturated
// Python raises RecursionError when a call would take more frames than the
// recursion limit. re._parser recurses once per group level through
// _parse_sub and _parse, and once per conditional through _parse; its
// `nested` argument counts those frames. Here a frame of re._parser has the
// number `nested` of the _parse or _parse_sub that holds it, or the number of
// its caller plus one. When Transform.apply or RespellRule.matches is called
// from the top level of a script, the frame after 993 raises RecursionError.
// The parser raises at the same points. The limit also bounds the C++ stack
// that the parser, the compiler and the matcher use.
constexpr int kMaxFrame = 993;

uint64_t sat_add(uint64_t a, uint64_t b) { return (a > kInf - b) ? kInf : a + b; }
uint64_t sat_mul(uint64_t a, uint64_t b) { return (a != 0 && b > kInf / a) ? kInf : a * b; }

// ── Parse tree, as re._parser builds it ─────────────────────────────────────

enum class Op { Literal, NotLiteral, Any, In, At, Branch, Subpattern, Repeat, GroupRef, GroupRefExists, Assert,
                AssertNot, Atomic };
enum class AtCode { Beginning, BeginningString, Boundary, NonBoundary, End, EndString };
enum class Cat { Digit, NotDigit, Space, NotSpace, Word, NotWord };
enum class RepKind { Max, Min, Possessive };

struct SetItem {
    enum Kind { Lit, Range, Category, Negate } kind = Lit;
    char32_t lo = 0, hi = 0;
    Cat cat = Cat::Digit;
    bool operator==(const SetItem& o) const {
        return kind == o.kind && lo == o.lo && hi == o.hi && (kind != Category || cat == o.cat);
    }
};

struct Node;
using Seq = std::vector<Node>;

struct Node {
    Op op = Op::Literal;
    char32_t ch = 0;
    std::vector<SetItem> set;
    AtCode at = AtCode::Beginning;
    std::vector<Seq> kids;  // Branch alternatives; the one body otherwise; yes/no of GroupRefExists
    int group = 0;          // 0: no capture
    int add_flags = 0, del_flags = 0;
    uint64_t min = 0, max = 0;
    RepKind rep = RepKind::Max;
    int dir = 1;
    bool has_no = false;
};

struct Width {
    uint64_t lo = 0, hi = 0;
};

struct State {
    int flags = 0;
    std::map<U32, int> groupdict;
    std::vector<std::optional<Width>> groupwidths{std::nullopt};
    std::optional<int> lookbehindgroups;
    std::map<uint64_t, size_t> grouprefpos;

    int groups() const { return static_cast<int>(groupwidths.size()); }
    bool checkgroup(int gid) const { return gid < groups() && groupwidths[static_cast<size_t>(gid)].has_value(); }
};

Width getwidth(const Seq& seq, const State& state);

Width getwidth(const Seq& seq, const State& state) {
    uint64_t lo = 0, hi = 0;
    for (const Node& n : seq) {
        switch (n.op) {
            case Op::Branch: {
                uint64_t i = kInf, j = 0;
                for (const Seq& alt : n.kids) {
                    const Width w = getwidth(alt, state);
                    i = std::min(i, w.lo);
                    j = std::max(j, w.hi);
                }
                lo = sat_add(lo, i);
                hi = sat_add(hi, j);
                break;
            }
            case Op::Atomic:
            case Op::Subpattern: {
                const Width w = getwidth(n.kids[0], state);
                lo = sat_add(lo, w.lo);
                hi = sat_add(hi, w.hi);
                break;
            }
            case Op::Repeat: {
                const Width w = getwidth(n.kids[0], state);
                lo = sat_add(lo, sat_mul(w.lo, n.min));
                if (n.max == kMaxRepeat && w.hi) {
                    hi = kInf;
                } else {
                    hi = sat_add(hi, sat_mul(w.hi, n.max));
                }
                break;
            }
            case Op::Literal:
            case Op::NotLiteral:
            case Op::Any:
            case Op::In:
                lo = sat_add(lo, 1);
                hi = sat_add(hi, 1);
                break;
            case Op::GroupRef: {
                const Width w = *state.groupwidths[static_cast<size_t>(n.group)];
                lo = sat_add(lo, w.lo);
                hi = sat_add(hi, w.hi);
                break;
            }
            case Op::GroupRefExists: {
                Width w = getwidth(n.kids[0], state);
                if (n.has_no) {
                    const Width v = getwidth(n.kids[1], state);
                    w.lo = std::min(w.lo, v.lo);
                    w.hi = std::max(w.hi, v.hi);
                } else {
                    w.lo = 0;
                }
                lo = sat_add(lo, w.lo);
                hi = sat_add(hi, w.hi);
                break;
            }
            default:
                break;
        }
    }
    return {lo, hi};
}

// ── Tokenizer and parser: a port of re._parser ─────────────────────────────

const U32View kSpecialChars = U".\\[{()*+?^$|";
const U32View kRepeatChars = U"*+?{";
const U32View kWhitespace = U" \t\n\r\v\f";

bool is_ascii_digit(char32_t c) { return c >= U'0' && c <= U'9'; }
bool is_oct_digit(char32_t c) { return c >= U'0' && c <= U'7'; }
bool is_hex_digit(char32_t c) {
    return is_ascii_digit(c) || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F');
}
bool is_ascii_letter(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'); }

int flag_of(char32_t c) {
    switch (c) {
        case U'i': return IGNORECASE;
        case U'L': return LOCALE;
        case U'm': return MULTILINE;
        case U's': return DOTALL;
        case U'x': return VERBOSE;
        case U'a': return ASCII;
        case U't': return 1;  // SRE_FLAG_TEMPLATE
        case U'u': return UNICODE;
        default: return 0;
    }
}
constexpr int kTypeFlags = ASCII | LOCALE | UNICODE;
constexpr int kGlobalFlags = 1 | 128;  // TEMPLATE | DEBUG

// re.error(msg): an error with no position, as re._compiler raises it.
[[noreturn]] void raise(const std::string& message) { throw error(message); }

// re.error(msg, pattern, pos): the message gains " at position <pos>", and a
// pattern with a newline adds the line and the column of that position.
[[noreturn]] void raise_at(const std::string& message, U32View pattern, std::ptrdiff_t pos) {
    std::string out = message + " at position " + std::to_string(pos);
    if (pattern.find(U'\n') != U32View::npos) {
        const size_t end = static_cast<size_t>(std::max<std::ptrdiff_t>(pos, 0));
        const U32View head = pattern.substr(0, end);
        const auto lineno = std::count(head.begin(), head.end(), U'\n') + 1;
        const size_t last = head.rfind(U'\n');
        const std::ptrdiff_t colno = pos - (last == U32View::npos ? -1 : static_cast<std::ptrdiff_t>(last));
        out += " (line " + std::to_string(lineno) + ", column " + std::to_string(colno) + ")";
    }
    throw error(out);
}

std::string str(U32View s) { return detail::s8(s); }

// "%d" % int(digits): the decimal digits without their leading zeros, of any
// length.
std::string decimal(const U32& digits) {
    const size_t first = digits.find_first_not_of(U'0');
    return first == U32::npos ? "0" : str(digits.substr(first));
}

// A token: one character, or a backslash and the character after it.
using Token = std::optional<U32>;

bool is_digits_token(const Token& t) { return t && t->size() == 1 && is_ascii_digit((*t)[0]); }
bool is_oct_token(const Token& t) { return t && t->size() == 1 && is_oct_digit((*t)[0]); }

class Tokenizer {
public:
    explicit Tokenizer(U32View s) : s_(s) { advance(); }

    // Python's frame for one re._parser function while it runs.
    class Call {
    public:
        Call(Tokenizer& source, int frame) : source_(source), saved_(source.frame_) { source.frame_ = frame; }
        explicit Call(Tokenizer& source) : Call(source, source.frame_ + 1) {}
        ~Call() { source_.frame_ = saved_; }
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;

    private:
        Tokenizer& source_;
        int saved_;
    };

    void set_frame(int frame) { frame_ = frame; }
    // RecursionError when Python would call `depth` frames below the current one.
    void need(int depth) const {
        if (frame_ + depth > kMaxFrame) throw Error("RecursionError", "maximum recursion depth exceeded");
    }
    // raise source.error(message): Tokenizer.error and its call of tell.
    // source.error(msg, offset): an error at tell() - offset.
    [[noreturn]] void error(const std::string& message, size_t offset = 0) const {
        need(2);
        raise_here(message, offset);
    }
    [[noreturn]] void raise_here(const std::string& message, size_t offset) const {
        raise_at(message, s_, static_cast<std::ptrdiff_t>(tell()) - static_cast<std::ptrdiff_t>(offset));
    }

    const Token& next() const { return next_; }
    bool match(U32View c) {
        if (next_ && *next_ == c) {
            advance();
            return true;
        }
        return false;
    }
    Token get() {
        Token t = next_;
        advance();
        return t;
    }
    U32 getwhile(int n, bool (*pred)(char32_t)) {
        U32 out;
        for (int i = 0; i < n; ++i) {
            if (!next_ || next_->size() != 1 || !pred((*next_)[0])) break;
            out += *next_;
            advance();
        }
        return out;
    }
    U32 getuntil(char32_t terminator, const std::string& name) {
        U32 result;
        while (true) {
            Token c = get();
            if (!c) {
                need(3);
                if (result.empty()) raise_here("missing " + name, 0);
                raise_here("missing " + str(U32(1, terminator)) + ", unterminated name", result.size());
            }
            if (c->size() == 1 && (*c)[0] == terminator) {
                if (result.empty()) {
                    need(3);
                    raise_here("missing " + name, 1);
                }
                break;
            }
            result += *c;
        }
        return result;
    }
    size_t tell() const { return index_ - (next_ ? next_->size() : 0); }
    void seek(size_t index) {
        index_ = index;
        advance();
    }
    void checkgroupname(const U32& name, size_t offset) const {
        if (!detail::is_identifier(name)) {
            need(3);
            raise_here("bad character in group name " + detail::py_str_repr(name), name.size() + offset);
        }
    }

private:
    // Tokenizer.__next, which Python reaches through get, match, getwhile,
    // getuntil or seek.
    void advance() {
        need(2);
        if (index_ >= s_.size()) {
            next_.reset();
            return;
        }
        U32 c(1, s_[index_]);
        size_t index = index_;
        if (c[0] == U'\\') {
            ++index;
            if (index >= s_.size()) {
                need(3);
                raise_at("bad escape (end of pattern)", s_, static_cast<std::ptrdiff_t>(s_.size()) - 1);
            }
            c += s_[index];
        }
        index_ = index + 1;
        next_ = c;
    }

    U32View s_;
    size_t index_ = 0;
    Token next_;
    int frame_ = 0;
};

struct Code {
    enum Kind { Lit, Category, Ref, AtK } kind = Lit;
    char32_t value = 0;
    Cat cat = Cat::Digit;
    AtCode at = AtCode::Beginning;
    int group = 0;
};

std::optional<Code> escapes(const U32& e) {
    if (e.size() != 2) return std::nullopt;
    switch (e[1]) {
        case U'a': return Code{Code::Lit, 7};
        case U'b': return Code{Code::Lit, 8};
        case U'f': return Code{Code::Lit, 12};
        case U'n': return Code{Code::Lit, 10};
        case U'r': return Code{Code::Lit, 13};
        case U't': return Code{Code::Lit, 9};
        case U'v': return Code{Code::Lit, 11};
        case U'\\': return Code{Code::Lit, U'\\'};
        default: return std::nullopt;
    }
}

std::optional<Code> categories(const U32& e) {
    if (e.size() != 2) return std::nullopt;
    Code c;
    switch (e[1]) {
        case U'A': c.kind = Code::AtK; c.at = AtCode::BeginningString; return c;
        case U'b': c.kind = Code::AtK; c.at = AtCode::Boundary; return c;
        case U'B': c.kind = Code::AtK; c.at = AtCode::NonBoundary; return c;
        case U'Z': c.kind = Code::AtK; c.at = AtCode::EndString; return c;
        case U'd': c.kind = Code::Category; c.cat = Cat::Digit; return c;
        case U'D': c.kind = Code::Category; c.cat = Cat::NotDigit; return c;
        case U's': c.kind = Code::Category; c.cat = Cat::Space; return c;
        case U'S': c.kind = Code::Category; c.cat = Cat::NotSpace; return c;
        case U'w': c.kind = Code::Category; c.cat = Cat::Word; return c;
        case U'W': c.kind = Code::Category; c.cat = Cat::NotWord; return c;
        default: return std::nullopt;
    }
}

uint64_t parse_uint(const U32& digits, int base) {
    uint64_t v = 0;
    for (char32_t c : digits) {
        const uint64_t d = is_ascii_digit(c) ? c - U'0' : (c >= U'a' ? c - U'a' + 10 : c - U'A' + 10);
        v = (v > (kInf - d) / static_cast<uint64_t>(base)) ? kInf : v * static_cast<uint64_t>(base) + d;
    }
    return v;
}

// The \x, \u, \U and \N escapes shared by both escape parsers; nullopt when
// `escape` is none of them.
std::optional<Code> hex_escape(Tokenizer& source, U32& escape) {
    const char32_t c = escape[1];
    if (c == U'x') {
        escape += source.getwhile(2, is_hex_digit);
        if (escape.size() != 4) source.error("incomplete escape " + str(escape), escape.size());
        return Code{Code::Lit, static_cast<char32_t>(parse_uint(escape.substr(2), 16))};
    }
    if (c == U'u') {
        escape += source.getwhile(4, is_hex_digit);
        if (escape.size() != 6) source.error("incomplete escape " + str(escape), escape.size());
        return Code{Code::Lit, static_cast<char32_t>(parse_uint(escape.substr(2), 16))};
    }
    if (c == U'U') {
        escape += source.getwhile(8, is_hex_digit);
        if (escape.size() != 10) source.error("incomplete escape " + str(escape), escape.size());
        const uint64_t v = parse_uint(escape.substr(2), 16);
        if (v > 0x10FFFF) source.error("bad escape " + str(escape), escape.size());
        return Code{Code::Lit, static_cast<char32_t>(v)};
    }
    if (c == U'N') {
        // The syntax errors are re._parser's; the name lookup is not ported.
        source.need(2);  // match and getuntil
        if (!source.match(U"{")) source.error("missing {");
        source.getuntil(U'}', "character name");
        throw NotImplementedError("\\N{...} escapes are not supported");
    }
    return std::nullopt;
}

Code class_escape(Tokenizer& source, U32 escape) {
    const Tokenizer::Call call(source);
    if (auto code = escapes(escape)) return *code;
    if (auto code = categories(escape); code && code->kind == Code::Category) return *code;
    if (auto code = hex_escape(source, escape)) return *code;
    const char32_t c = escape[1];
    if (is_oct_digit(c)) {
        escape += source.getwhile(2, is_oct_digit);
        const uint64_t v = parse_uint(escape.substr(1), 8);
        if (v > 0377) source.error("octal escape value " + str(escape) + " outside of range 0-0o377", escape.size());
        return Code{Code::Lit, static_cast<char32_t>(v)};
    }
    if (is_ascii_digit(c)) source.error("bad escape " + str(escape), escape.size());
    if (escape.size() == 2) {
        if (is_ascii_letter(c)) source.error("bad escape " + str(escape), escape.size());
        return Code{Code::Lit, escape[1]};
    }
    source.error("bad escape " + str(escape), escape.size());
}

void checklookbehindgroup(Tokenizer& source, const State& state, int gid) {
    const Tokenizer::Call call(source);
    if (state.lookbehindgroups) {
        source.need(2);  // checkgroup and the groups property
        if (!state.checkgroup(gid)) source.error("cannot refer to an open group");
        if (gid >= *state.lookbehindgroups) source.error("cannot refer to group defined in the same lookbehind subpattern");
    }
}

Code escape_code(Tokenizer& source, U32 escape, State& state) {
    const Tokenizer::Call call(source);
    if (auto code = categories(escape)) return *code;
    if (auto code = escapes(escape)) return *code;
    if (auto code = hex_escape(source, escape)) return *code;
    const char32_t c = escape[1];
    if (c == U'0') {
        escape += source.getwhile(2, is_oct_digit);
        return Code{Code::Lit, static_cast<char32_t>(parse_uint(escape.substr(1), 8))};
    }
    if (is_ascii_digit(c)) {
        if (is_digits_token(source.next())) {
            escape += *source.get();
            if (is_oct_digit(escape[1]) && is_oct_digit(escape[2]) && is_oct_token(source.next())) {
                escape += *source.get();
                const uint64_t v = parse_uint(escape.substr(1), 8);
                if (v > 0377) source.error("octal escape value " + str(escape) + " outside of range 0-0o377", escape.size());
                return Code{Code::Lit, static_cast<char32_t>(v)};
            }
        }
        const uint64_t group = parse_uint(escape.substr(1), 10);
        if (group < static_cast<uint64_t>(state.groups())) {
            source.need(2);  // checkgroup and the groups property
            if (!state.checkgroup(static_cast<int>(group))) source.error("cannot refer to an open group", escape.size());
            checklookbehindgroup(source, state, static_cast<int>(group));
            Code code;
            code.kind = Code::Ref;
            code.group = static_cast<int>(group);
            return code;
        }
        source.error("invalid group reference " + std::to_string(group), escape.size() - 1);
    }
    if (escape.size() == 2) {
        if (is_ascii_letter(c)) source.error("bad escape " + str(escape), escape.size());
        return Code{Code::Lit, escape[1]};
    }
    source.error("bad escape " + str(escape), escape.size());
}

Node node_of(const Code& code) {
    Node n;
    switch (code.kind) {
        case Code::Lit: n.op = Op::Literal; n.ch = code.value; break;
        case Code::Category: {
            n.op = Op::In;
            SetItem item;
            item.kind = SetItem::Category;
            item.cat = code.cat;
            n.set.push_back(item);
            break;
        }
        case Code::Ref: n.op = Op::GroupRef; n.group = code.group; break;
        case Code::AtK: n.op = Op::At; n.at = code.at; break;
    }
    return n;
}

// _parse_flags: nullopt for global flags "(?flags)".
std::optional<std::pair<int, int>> parse_flags(Tokenizer& source, State& state, char32_t ch) {
    const Tokenizer::Call call(source);
    int add_flags = 0, del_flags = 0;
    Token t;
    if (ch != U'-') {
        while (true) {
            const int flag = flag_of(ch);
            if (ch == U'L') source.error("bad inline flags: cannot use 'L' flag with a str pattern");
            add_flags |= flag;
            if ((flag & kTypeFlags) && (add_flags & kTypeFlags) != flag)
                source.error("bad inline flags: flags 'a', 'u' and 'L' are incompatible");
            t = source.get();
            if (!t) source.error("missing -, : or )");
            ch = (*t)[0];
            if (t->size() == 1 && (ch == U')' || ch == U'-' || ch == U':')) break;
            if (t->size() != 1 || !flag_of(ch)) {
                source.error((t->size() == 1 && detail::is_alpha(ch)) ? "unknown flag" : "missing -, : or )", t->size());
            }
        }
    }
    if (ch == U')') {
        state.flags |= add_flags;
        return std::nullopt;
    }
    if (add_flags & kGlobalFlags) source.error("bad inline flags: cannot turn on global flag", 1);
    if (ch == U'-') {
        t = source.get();
        if (!t) source.error("missing flag");
        ch = (*t)[0];
        if (t->size() != 1 || !flag_of(ch))
            source.error((t->size() == 1 && detail::is_alpha(ch)) ? "unknown flag" : "missing flag", t->size());
        while (true) {
            const int flag = flag_of(ch);
            if (flag & kTypeFlags) source.error("bad inline flags: cannot turn off flags 'a', 'u' and 'L'");
            del_flags |= flag;
            t = source.get();
            if (!t) source.error("missing :");
            ch = (*t)[0];
            if (t->size() == 1 && ch == U':') break;
            if (t->size() != 1 || !flag_of(ch))
                source.error((t->size() == 1 && detail::is_alpha(ch)) ? "unknown flag" : "missing :", t->size());
        }
    }
    if (del_flags & kGlobalFlags) source.error("bad inline flags: cannot turn off global flag", 1);
    if (add_flags & del_flags) source.error("bad inline flags: flag turned on and off", 1);
    return std::make_pair(add_flags, del_flags);
}

bool is_token(const Token& t, char32_t c) { return t && t->size() == 1 && (*t)[0] == c; }

void parse_class(Tokenizer& source, Seq& out) {
    std::vector<SetItem> set;
    const size_t here = source.tell() - 1;  // the "["
    auto unterminated = [&] { source.error("unterminated character set", source.tell() - here); };
    const bool negate = source.match(U"^");
    auto to_item = [](const Code& code) {
        SetItem item;
        if (code.kind == Code::Category) {
            item.kind = SetItem::Category;
            item.cat = code.cat;
        } else {
            item.kind = SetItem::Lit;
            item.lo = item.hi = code.value;
        }
        return item;
    };
    while (true) {
        Token t = source.get();
        if (!t) unterminated();
        const U32 thiss = *t;
        if (thiss == U"]" && !set.empty()) break;
        Code code1;
        if (thiss[0] == U'\\' && thiss.size() == 2) {
            code1 = class_escape(source, thiss);
        } else {
            code1 = Code{Code::Lit, thiss[0]};
        }
        if (source.match(U"-")) {
            Token that = source.get();
            if (!that) unterminated();
            if (*that == U"]") {
                set.push_back(to_item(code1));
                set.push_back(to_item(Code{Code::Lit, U'-'}));
                break;
            }
            Code code2;
            if ((*that)[0] == U'\\' && that->size() == 2) {
                code2 = class_escape(source, *that);
            } else {
                code2 = Code{Code::Lit, (*that)[0]};
            }
            if (code1.kind != Code::Lit || code2.kind != Code::Lit || code2.value < code1.value)
                source.error("bad character range " + str(thiss) + "-" + str(*that), thiss.size() + 1 + that->size());
            SetItem range;
            range.kind = SetItem::Range;
            range.lo = code1.value;
            range.hi = code2.value;
            set.push_back(range);
        } else {
            set.push_back(to_item(code1));
        }
    }
    std::vector<SetItem> uniq;
    for (const auto& item : set) {
        if (std::find(uniq.begin(), uniq.end(), item) == uniq.end()) uniq.push_back(item);
    }
    Node n;
    if (uniq.size() == 1 && uniq[0].kind == SetItem::Lit) {
        n.op = negate ? Op::NotLiteral : Op::Literal;
        n.ch = uniq[0].lo;
    } else {
        n.op = Op::In;
        if (negate) {
            SetItem neg;
            neg.kind = SetItem::Negate;
            uniq.insert(uniq.begin(), neg);
        }
        n.set = std::move(uniq);
    }
    out.push_back(std::move(n));
}

// re._parser's _parse_sub and _parse recurse once per nesting level. Here each
// open call is an entry of a heap stack, so the C++ stack does not grow with
// the nesting of the pattern.
struct ParseCall {
    bool sub = false;  // _parse_sub; _parse otherwise
    int nested = 0;
    bool verbose = false;
    bool first = false;       // _parse: the first item of the pattern
    std::vector<Seq> items;   // _parse_sub: the alternatives so far
    Seq seq;                  // _parse: the items so far
    // _parse: the group whose body the call above this one parses.
    enum class Open { None, Group, Look, CondYes, CondNo } open = Open::None;
    int group = 0;
    bool atomic = false;
    int add_flags = 0, del_flags = 0;
    int dir = 1;
    char32_t look = 0;
    bool lookbehind_set = false;  // this lookbehind set state.lookbehindgroups
    Node cond;                    // the GroupRefExists that the call above fills
    size_t start = 0;             // the position of the "(" of that group
};

struct ChildCall {
    bool sub;
    int nested;
    bool verbose;
    bool first;
};

// The loop of _parse, up to the end of its items or up to a group, whose body
// a child call parses.
std::optional<ChildCall> parse_items(Tokenizer& source, State& state, ParseCall& call) {
    Seq& sub = call.seq;
    bool& verbose = call.verbose;
    const int nested = call.nested;
    const bool first = call.first;
    while (true) {
        const Token& peek = source.next();
        if (!peek) break;
        if (is_token(peek, U'|') || is_token(peek, U')')) break;
        const U32 thiss = *source.get();
        if (verbose) {
            if (thiss.size() == 1 && kWhitespace.find(thiss[0]) != U32View::npos) continue;
            if (thiss == U"#") {
                while (true) {
                    Token t = source.get();
                    if (!t || *t == U"\n") break;
                }
                continue;
            }
        }
        if (thiss[0] == U'\\' && thiss.size() == 2) {
            sub.push_back(node_of(escape_code(source, thiss, state)));
            continue;
        }
        const char32_t c = thiss[0];
        if (kSpecialChars.find(c) == U32View::npos) {
            Node n;
            n.op = Op::Literal;
            n.ch = c;
            sub.push_back(n);
        } else if (c == U'[') {
            parse_class(source, sub);
        } else if (kRepeatChars.find(c) != U32View::npos) {
            const size_t here = source.tell();
            uint64_t min = 0, max = 0;
            if (c == U'?') {
                min = 0, max = 1;
            } else if (c == U'*') {
                min = 0, max = kMaxRepeat;
            } else if (c == U'+') {
                min = 1, max = kMaxRepeat;
            } else {
                if (is_token(source.next(), U'}')) {
                    Node n;
                    n.op = Op::Literal;
                    n.ch = c;
                    sub.push_back(n);
                    continue;
                }
                min = 0, max = kMaxRepeat;
                U32 lo, hi;
                while (is_digits_token(source.next())) lo += *source.get();
                if (source.match(U",")) {
                    while (is_digits_token(source.next())) hi += *source.get();
                } else {
                    hi = lo;
                }
                if (!source.match(U"}")) {
                    Node n;
                    n.op = Op::Literal;
                    n.ch = c;
                    sub.push_back(n);
                    source.seek(here);
                    continue;
                }
                if (!lo.empty()) {
                    min = parse_uint(lo, 10);
                    if (min >= kMaxRepeat) throw Error("OverflowError", "the repetition number is too large");
                }
                if (!hi.empty()) {
                    max = parse_uint(hi, 10);
                    if (max >= kMaxRepeat) throw Error("OverflowError", "the repetition number is too large");
                    if (max < min) source.error("min repeat greater than max repeat", source.tell() - here);
                }
            }
            if (sub.empty() || sub.back().op == Op::At) source.error("nothing to repeat", source.tell() - here + thiss.size());
            if (sub.back().op == Op::Repeat) source.error("multiple repeat", source.tell() - here + thiss.size());
            Seq item;
            Node& last = sub.back();
            if (last.op == Op::Subpattern && last.group == 0 && !last.add_flags && !last.del_flags) {
                item = std::move(last.kids[0]);
            } else {
                item.push_back(std::move(last));
            }
            Node rep;
            rep.op = Op::Repeat;
            rep.min = min;
            rep.max = max;
            rep.kids.push_back(std::move(item));
            if (source.match(U"?")) {
                rep.rep = RepKind::Min;
            } else if (source.match(U"+")) {
                rep.rep = RepKind::Possessive;
            } else {
                rep.rep = RepKind::Max;
            }
            sub.back() = std::move(rep);
        } else if (c == U'.') {
            Node n;
            n.op = Op::Any;
            sub.push_back(n);
        } else if (c == U'(') {
            const size_t start = source.tell() - 1;
            bool capture = true, atomic = false;
            std::optional<U32> name;
            int add_flags = 0, del_flags = 0;
            if (source.match(U"?")) {
                Token ct = source.get();
                if (!ct) source.error("unexpected end of pattern");
                const U32 chs = *ct;
                const char32_t ch = chs[0];
                const bool plain = chs.size() == 1;
                if (plain && ch == U'P') {
                    if (source.match(U"<")) {
                        name = source.getuntil(U'>', "group name");
                        source.checkgroupname(*name, 1);
                    } else if (source.match(U"=")) {
                        const U32 ref = source.getuntil(U')', "group name");
                        source.checkgroupname(ref, 1);
                        const auto it = state.groupdict.find(ref);
                        if (it == state.groupdict.end())
                            source.error("unknown group name " + detail::py_str_repr(ref), ref.size() + 1);
                        if (!state.checkgroup(it->second)) source.error("cannot refer to an open group", ref.size() + 1);
                        checklookbehindgroup(source, state, it->second);
                        Node n;
                        n.op = Op::GroupRef;
                        n.group = it->second;
                        sub.push_back(n);
                        continue;
                    } else {
                        Token t = source.get();
                        if (!t) source.error("unexpected end of pattern");
                        source.error("unknown extension ?P" + str(*t), t->size() + 2);
                    }
                } else if (plain && ch == U':') {
                    capture = false;
                } else if (plain && ch == U'#') {
                    while (true) {
                        if (!source.next()) source.error("missing ), unterminated comment", source.tell() - start);
                        if (is_token(source.get(), U')')) break;
                    }
                    continue;
                } else if (plain && (ch == U'=' || ch == U'!' || ch == U'<')) {
                    int dir = 1;
                    char32_t kind = ch;
                    std::optional<int> saved_lookbehind = state.lookbehindgroups;
                    if (ch == U'<') {
                        Token t = source.get();
                        if (!t) source.error("unexpected end of pattern");
                        if (!(is_token(t, U'=') || is_token(t, U'!'))) source.error("unknown extension ?<" + str(*t), t->size() + 2);
                        kind = (*t)[0];
                        dir = -1;
                        if (!saved_lookbehind) state.lookbehindgroups = state.groups();
                    }
                    call.open = ParseCall::Open::Look;
                    call.start = start;
                    call.dir = dir;
                    call.look = kind;
                    call.lookbehind_set = dir < 0 && !saved_lookbehind;
                    return ChildCall{true, nested + 1, verbose, false};
                } else if (plain && ch == U'(') {
                    const U32 condname = source.getuntil(U')', "group name");
                    int condgroup;
                    const bool numeric = std::all_of(condname.begin(), condname.end(), is_ascii_digit);
                    if (!numeric) {
                        source.checkgroupname(condname, 1);
                        const auto it = state.groupdict.find(condname);
                        if (it == state.groupdict.end())
                            source.error("unknown group name " + detail::py_str_repr(condname), condname.size() + 1);
                        condgroup = it->second;
                    } else {
                        const uint64_t g = parse_uint(condname, 10);
                        if (g == 0) source.error("bad group number", condname.size() + 1);
                        if (g >= kMaxGroups) source.error("invalid group reference " + decimal(condname), condname.size() + 1);
                        if (!state.grouprefpos.count(g)) state.grouprefpos[g] = source.tell() - condname.size() - 1;
                        condgroup = static_cast<int>(g);
                    }
                    checklookbehindgroup(source, state, condgroup);
                    call.open = ParseCall::Open::CondYes;
                    call.start = start;
                    call.cond = Node();
                    call.cond.op = Op::GroupRefExists;
                    call.cond.group = condgroup;
                    return ChildCall{false, nested + 1, verbose, false};
                } else if (plain && ch == U'>') {
                    capture = false;
                    atomic = true;
                } else if (plain && (flag_of(ch) || ch == U'-')) {
                    const auto flags = parse_flags(source, state, ch);
                    if (!flags) {
                        if (!first || !sub.empty())
                            source.error("global flags not at the start of the expression", source.tell() - start);
                        verbose = state.flags & VERBOSE;
                        continue;
                    }
                    add_flags = flags->first;
                    del_flags = flags->second;
                    capture = false;
                } else {
                    source.error("unknown extension ?" + str(chs), chs.size() + 1);
                }
            }
            int group = 0;
            if (capture) {
                group = state.groups();
                state.groupwidths.emplace_back(std::nullopt);
                if (static_cast<uint64_t>(state.groups()) > kMaxGroups) raise("too many groups");
                if (name) {
                    if (const auto it = state.groupdict.find(*name); it != state.groupdict.end()) {
                        source.error("redefinition of group name " + detail::py_str_repr(*name) + " as group " +
                                         std::to_string(group) + "; was group " + std::to_string(it->second),
                                     name->size() + 1);
                    }
                    state.groupdict[*name] = group;
                }
            }
            const bool sub_verbose = (verbose || (add_flags & VERBOSE)) && !(del_flags & VERBOSE);
            call.open = ParseCall::Open::Group;
            call.start = start;
            call.group = group;
            call.atomic = atomic;
            call.add_flags = add_flags;
            call.del_flags = del_flags;
            return ChildCall{true, nested + 1, sub_verbose, false};
        } else if (c == U'^') {
            Node n;
            n.op = Op::At;
            n.at = AtCode::Beginning;
            sub.push_back(n);
        } else if (c == U'$') {
            Node n;
            n.op = Op::At;
            n.at = AtCode::End;
            sub.push_back(n);
        }
    }
    return std::nullopt;
}

// The end of the group whose body `body` is: _parse after its child call.
// A conditional with a "no" branch needs a second child call.
std::optional<ChildCall> close_group(Tokenizer& source, State& state, ParseCall& call, Seq body) {
    Node n;
    switch (call.open) {
        case ParseCall::Open::Group:
            if (!source.match(U")")) source.error("missing ), unterminated subpattern", source.tell() - call.start);
            if (call.group) state.groupwidths[static_cast<size_t>(call.group)] = getwidth(body, state);
            n.op = call.atomic ? Op::Atomic : Op::Subpattern;
            n.group = call.group;
            n.add_flags = call.add_flags;
            n.del_flags = call.del_flags;
            n.kids.push_back(std::move(body));
            break;
        case ParseCall::Open::Look:
            if (call.lookbehind_set) state.lookbehindgroups.reset();
            if (!source.match(U")")) source.error("missing ), unterminated subpattern", source.tell() - call.start);
            n.op = call.look == U'=' ? Op::Assert : Op::AssertNot;
            n.dir = call.dir;
            n.kids.push_back(std::move(body));
            break;
        case ParseCall::Open::CondYes:
            call.cond.kids.push_back(std::move(body));
            if (source.match(U"|")) {
                call.open = ParseCall::Open::CondNo;
                return ChildCall{false, call.nested + 1, call.verbose, false};
            }
            if (!source.match(U")")) source.error("missing ), unterminated subpattern", source.tell() - call.start);
            n = std::move(call.cond);
            break;
        case ParseCall::Open::CondNo:
            call.cond.kids.push_back(std::move(body));
            call.cond.has_no = true;
            if (is_token(source.next(), U'|')) source.error("conditional backref with more than two branches");
            if (!source.match(U")")) source.error("missing ), unterminated subpattern", source.tell() - call.start);
            n = std::move(call.cond);
            break;
        case ParseCall::Open::None:
            break;
    }
    call.open = ParseCall::Open::None;
    call.seq.push_back(std::move(n));
    return std::nullopt;
}

// _parse_sub(source, state, verbose, 0).
Seq parse(Tokenizer& source, State& state, bool verbose) {
    std::vector<ParseCall> calls;
    auto open = [&](const ChildCall& c) {
        source.set_frame(c.nested);
        if (!c.sub) source.need(1);  // SubPattern(state)
        ParseCall call;
        call.sub = c.sub;
        call.nested = c.nested;
        call.verbose = c.verbose;
        call.first = c.first;
        calls.push_back(std::move(call));
    };
    open(ChildCall{true, 0, verbose, false});
    Seq ret;              // the result of the call that returned last
    bool returned = false;
    while (true) {
        ParseCall& call = calls.back();
        source.set_frame(call.nested);
        std::optional<ChildCall> child;
        if (call.sub) {
            if (!returned) {
                child = ChildCall{false, call.nested + 1, call.verbose, call.nested == 0};
            } else {
                call.items.push_back(std::move(ret));
                if (source.match(U"|")) {
                    if (call.nested == 0) call.verbose = state.flags & VERBOSE;
                    child = ChildCall{false, call.nested + 1, call.verbose, false};
                } else if (call.items.size() == 1) {
                    ret = std::move(call.items[0]);
                } else {
                    Node branch;
                    branch.op = Op::Branch;
                    branch.kids = std::move(call.items);
                    ret = Seq{};
                    ret.push_back(std::move(branch));
                }
            }
        } else {
            if (returned) child = close_group(source, state, call, std::move(ret));
            if (!child) child = parse_items(source, state, call);
            if (!child) {
                // Splice in the subpatterns that neither capture nor change flags.
                ret = Seq{};
                for (Node& n : call.seq) {
                    if (n.op == Op::Subpattern && n.group == 0 && !n.add_flags && !n.del_flags) {
                        for (Node& k : n.kids[0]) ret.push_back(std::move(k));
                    } else {
                        ret.push_back(std::move(n));
                    }
                }
            }
        }
        if (child) {
            open(*child);  // `call` dangles from here on
            returned = false;
            continue;
        }
        calls.pop_back();
        returned = true;
        if (calls.empty()) return ret;
    }
}

// ── Compiled program: the parse tree with the flags resolved ──────────────

struct Charset {
    enum Mode { Plain, UniIgnore, AsciiIgnore } mode = Plain;
    bool negate = false;
    bool ascii_cats = false;
    std::vector<SetItem> items;   // Plain: literals, ranges and categories
    std::vector<uint64_t> bitmap;  // the ignore modes: the lower-cased BMP members
    std::vector<SetItem> tail;     // the ignore modes: astral members and categories
    bool range_uni_ignore = false;
};

struct MNode;
using MSeq = std::vector<MNode>;

struct MNode {
    enum Kind { Lit, NotLit, Any, In, At, Branch, Mark, Repeat, Ref, RefExists, Look, Atomic } kind = Lit;
    // Lit/NotLit: 0 exact, 1 lower(ch) == c, 2 ascii lower, 3 lower(ch) in cset.
    int cmode = 0;
    char32_t c = 0;
    std::vector<char32_t> cset;
    bool dotall = false;
    std::shared_ptr<Charset> charset;
    AtCode at = AtCode::Beginning;
    bool multiline = false, ascii = false;
    std::vector<MSeq> kids;
    int group = 0;
    uint64_t min = 0, max = 0;
    RepKind rep = RepKind::Max;
    bool behind = false, negative = false, has_no = false;
    uint64_t width = 0;
    int refmode = 0;  // 0 exact, 1 Unicode lower, 2 ASCII lower
};

}  // namespace impl

struct Program {
    impl::MSeq code;
    int groups = 1;
};

namespace impl {

char32_t ascii_lower(char32_t c) { return (c >= U'A' && c <= U'Z') ? c + 32 : c; }
bool ascii_iscased(char32_t c) { return is_ascii_letter(c); }

bool category(Cat cat, char32_t c, bool ascii) {
    switch (cat) {
        case Cat::Digit: return ascii ? is_ascii_digit(c) : detail::is_decimal(c);
        case Cat::NotDigit: return !(ascii ? is_ascii_digit(c) : detail::is_decimal(c));
        case Cat::Space:
        case Cat::NotSpace: {
            const bool sp = ascii ? (c < 128 && kWhitespace.find(c) != U32View::npos) : detail::is_space(c);
            return cat == Cat::Space ? sp : !sp;
        }
        case Cat::Word:
        case Cat::NotWord: {
            const bool w = ascii ? (c < 128 && (is_ascii_letter(c) || is_ascii_digit(c) || c == U'_'))
                                 : detail::is_word(c);
            return cat == Cat::Word ? w : !w;
        }
    }
    return false;
}

bool charset_has(const Charset& cs, char32_t ch) {
    bool hit = false;
    if (cs.mode == Charset::Plain) {
        for (const SetItem& item : cs.items) {
            if ((item.kind == SetItem::Lit && item.lo == ch) ||
                (item.kind == SetItem::Range && item.lo <= ch && ch <= item.hi) ||
                (item.kind == SetItem::Category && category(item.cat, ch, cs.ascii_cats))) {
                hit = true;
                break;
            }
        }
    } else {
        const char32_t lo = cs.mode == Charset::UniIgnore ? detail::sre_lower(ch) : ascii_lower(ch);
        if (lo < 0x10000 && ((cs.bitmap[lo >> 6] >> (lo & 63)) & 1)) hit = true;
        for (size_t i = 0; !hit && i < cs.tail.size(); ++i) {
            const SetItem& item = cs.tail[i];
            if (item.kind == SetItem::Lit) {
                hit = item.lo == lo;
            } else if (item.kind == SetItem::Range) {
                hit = (item.lo <= lo && lo <= item.hi);
                if (!hit && cs.mode == Charset::UniIgnore) {
                    const char32_t up = detail::sre_upper(lo);
                    hit = item.lo <= up && up <= item.hi;
                }
            } else if (item.kind == SetItem::Category) {
                hit = category(item.cat, lo, cs.ascii_cats);
            }
        }
    }
    return hit != cs.negate;
}

std::shared_ptr<Charset> compile_charset(const std::vector<SetItem>& set, int flags) {
    auto cs = std::make_shared<Charset>();
    cs->ascii_cats = !(flags & UNICODE);
    const bool icase = flags & IGNORECASE;
    const bool uni = flags & UNICODE;
    for (const SetItem& item : set) {
        if (item.kind == SetItem::Negate) cs->negate = true;
    }
    if (!icase) {
        for (const SetItem& item : set) {
            if (item.kind != SetItem::Negate) cs->items.push_back(item);
        }
        return cs;
    }
    auto fixup = [&](char32_t c) { return uni ? detail::sre_lower(c) : ascii_lower(c); };
    auto iscased = [&](char32_t c) { return uni ? detail::sre_iscased(c) : ascii_iscased(c); };
    std::vector<uint64_t> bitmap(0x10000 / 64, 0);
    std::vector<SetItem> tail;
    bool hascased = false;
    auto mark = [&](char32_t c) {
        bitmap[c >> 6] |= uint64_t{1} << (c & 63);
        if (uni) {
            for (char32_t k : detail::sre_case_fixes(c)) bitmap[k >> 6] |= uint64_t{1} << (k & 63);
        }
    };
    for (const SetItem& item : set) {
        if (item.kind == SetItem::Lit) {
            const char32_t lo = fixup(item.lo);
            if (lo < 0x10000) {
                mark(lo);
            } else {
                SetItem t = item;
                t.lo = t.hi = lo;
                tail.push_back(t);
            }
            if (iscased(lo)) hascased = true;
        } else if (item.kind == SetItem::Range) {
            bool astral = false;
            for (char32_t c = item.lo;; ++c) {
                const char32_t lo = fixup(c);
                if (lo >= 0x10000) {
                    astral = true;
                    break;
                }
                mark(lo);
                if (c == item.hi) break;
            }
            if (astral) {
                tail.push_back(item);
                hascased = true;
                if (uni) cs->range_uni_ignore = true;
            } else if (!hascased) {
                for (char32_t c = item.lo;; ++c) {
                    if (iscased(c)) {
                        hascased = true;
                        break;
                    }
                    if (c == item.hi) break;
                }
            }
        } else if (item.kind == SetItem::Category) {
            tail.push_back(item);
        }
    }
    if (!hascased) {
        for (const SetItem& item : set) {
            if (item.kind != SetItem::Negate) cs->items.push_back(item);
        }
        return cs;
    }
    cs->mode = uni ? Charset::UniIgnore : Charset::AsciiIgnore;
    cs->bitmap = std::move(bitmap);
    cs->tail = std::move(tail);
    return cs;
}

int combine_flags(int flags, int add_flags, int del_flags) {
    if (add_flags & kTypeFlags) flags &= ~kTypeFlags;
    return (flags | add_flags) & ~del_flags;
}

MNode compile_literal(const Node& n, int flags) {
    MNode m;
    m.kind = n.op == Op::Literal ? MNode::Lit : MNode::NotLit;
    m.c = n.ch;
    if (!(flags & IGNORECASE)) return m;
    const bool uni = flags & UNICODE;
    if (uni ? !detail::sre_iscased(n.ch) : !ascii_iscased(n.ch)) return m;
    if (!uni) {
        m.cmode = 2;
        m.c = ascii_lower(n.ch);
        return m;
    }
    const char32_t lo = detail::sre_lower(n.ch);
    const auto fixes = detail::sre_case_fixes(lo);
    m.c = lo;
    if (fixes.empty()) {
        m.cmode = 1;
    } else {
        m.cmode = 3;
        m.cset.push_back(lo);
        m.cset.insert(m.cset.end(), fixes.begin(), fixes.end());
    }
    return m;
}

// A compiled node without its kids. `kid_flags` gets the flags its kids
// compile with.
MNode compile_node(const Node& n, int flags, const State& state, int& kid_flags) {
    kid_flags = flags;
    MNode m;
    switch (n.op) {
        case Op::Literal:
        case Op::NotLiteral:
            return compile_literal(n, flags);
        case Op::Any:
            m.kind = MNode::Any;
            m.dotall = flags & DOTALL;
            return m;
        case Op::In:
            m.kind = MNode::In;
            m.charset = compile_charset(n.set, flags);
            return m;
        case Op::At:
            m.kind = MNode::At;
            m.at = n.at;
            m.multiline = flags & MULTILINE;
            m.ascii = !(flags & UNICODE);
            return m;
        case Op::Branch:
            m.kind = MNode::Branch;
            return m;
        case Op::Subpattern:
            kid_flags = combine_flags(flags, n.add_flags, n.del_flags);
            // A flag-only group: its body runs inline.
            m.kind = n.group == 0 ? MNode::Branch : MNode::Mark;
            m.group = n.group;
            return m;
        case Op::Repeat:
            m.kind = MNode::Repeat;
            m.min = n.min;
            m.max = n.max;
            m.rep = n.rep;
            return m;
        case Op::GroupRef:
            m.kind = MNode::Ref;
            m.group = n.group;
            m.refmode = !(flags & IGNORECASE) ? 0 : (flags & UNICODE) ? 1 : 2;
            return m;
        case Op::GroupRefExists:
            m.kind = MNode::RefExists;
            m.group = n.group;
            m.has_no = n.has_no;
            return m;
        case Op::Assert:
        case Op::AssertNot: {
            m.kind = MNode::Look;
            m.negative = n.op == Op::AssertNot;
            m.behind = n.dir < 0;
            if (m.behind) {
                const Width w = getwidth(n.kids[0], state);
                if (w.lo > kMaxCode) raise("looks too much behind");
                if (w.lo != w.hi) raise("look-behind requires fixed-width pattern");
                m.width = w.lo;
            }
            return m;
        }
        case Op::Atomic:
            m.kind = MNode::Atomic;
            return m;
    }
    return m;
}

// Compiles the parse tree depth first, in the order a recursive compiler
// would, with a heap stack of the open nodes, so the C++ stack does not grow
// with the nesting of the pattern.
MSeq compile_seq(const Seq& root, int flags, const State& state) {
    struct Open {
        const Seq* kids;  // the kid sequences of the node
        size_t count;     // how many there are
        int flags;        // the flags they compile with
        MNode m;          // the node, with the kids compiled so far
        size_t k = 0;     // the kid sequence being compiled
        size_t i = 0;     // its next node
        MSeq out{};       // its compiled nodes so far
    };
    std::vector<Open> open;
    open.push_back(Open{&root, 1, flags, MNode()});
    while (true) {
        Open& o = open.back();
        const Seq& seq = o.kids[o.k];
        if (o.i < seq.size()) {
            const Node& n = seq[o.i++];
            int kid_flags;
            MNode m = compile_node(n, o.flags, state, kid_flags);
            if (n.kids.empty()) {
                o.out.push_back(std::move(m));
            } else {
                open.push_back(Open{n.kids.data(), n.kids.size(), kid_flags, std::move(m)});  // `o` dangles now
            }
            continue;
        }
        o.m.kids.push_back(std::move(o.out));
        o.out = MSeq();
        if (++o.k < o.count) {
            o.i = 0;
            continue;
        }
        MNode done = std::move(o.m);
        open.pop_back();
        if (open.empty()) return std::move(done.kids[0]);
        open.back().out.push_back(std::move(done));
    }
}

// ── Matcher ─────────────────────────────────────────────────────────────────

// The matcher backtracks on heap stacks, so its C++ stack depth does not
// grow with the subject: a continuation is a chain of frames in `frames_`, a
// backtrack point is an entry in `choices_`, and every group mark change is
// logged in `trail_` so that a backtrack restores the marks. Only lookaround,
// atomic groups and possessive repeats run a nested match, so the C++ depth
// grows with the nesting of the pattern alone.
class Matcher {
public:
    Matcher(const Program& program, U32View s)
        : program_(program), s_(s), marks_(static_cast<size_t>(program.groups) * 2, -1) {}

    // Search from `from`; on success marks()[0..1] hold the match span.
    bool search(size_t from, bool must_advance) {
        for (size_t start = from; start <= s_.size(); ++start) {
            std::fill(marks_.begin(), marks_.end(), -1);
            frames_.clear();
            choices_.clear();
            trail_.clear();
            start_ = start;
            require_progress_ = must_advance && start == from;
            size_t end = 0;
            if (run(program_.code, start, push_frame({Frame::Accept}), end)) {
                marks_[0] = static_cast<long>(start);
                marks_[1] = static_cast<long>(end);
                return true;
            }
        }
        return false;
    }

    const std::vector<long>& marks() const { return marks_; }

private:
    enum class Mode { Eval, Return, Fail };

    // What to do with the position a sub-match ends at.
    struct Frame {
        enum Kind { Accept, SubEnd, Seq, Close, Rep } kind;
        const MSeq* q = nullptr;   // Seq: go on with q[i:]
        size_t i = 0;
        size_t mark = 0;           // Close: the end mark to set
        const MNode* n = nullptr;  // Rep: one more iteration of n ended
        uint64_t count = 0;        // Rep: the iteration count after it
        long last = -1;            // Rep: where the latest optional iteration began
        size_t parent = 0;
    };
    // A backtrack point.
    struct Choice {
        enum Kind { Branch, RepeatSkip, RepeatMore } kind;
        const MNode* n;
        size_t alt;    // Branch: the next alternative
        size_t pos;
        size_t k;      // the continuation
        uint64_t count;
        long last;
        size_t trail;  // trail_ and frames_ sizes when the point was made
        size_t frames;
    };

    size_t push_frame(Frame f) {
        frames_.push_back(f);
        return frames_.size() - 1;
    }

    void push_choice(Choice c) {
        c.trail = trail_.size();
        c.frames = frames_.size();
        choices_.push_back(c);
    }

    void set_mark(size_t index, long value) {
        trail_.emplace_back(index, marks_[index]);
        marks_[index] = value;
    }

    void undo(size_t height) {
        while (trail_.size() > height) {
            marks_[trail_.back().first] = trail_.back().second;
            trail_.pop_back();
        }
    }

    // Match q from pos, then continuation k. On success `end` is where the
    // Accept or SubEnd frame was reached, the backtrack points made inside
    // are dropped and the mark changes are kept on the trail. On failure the
    // marks are as on entry.
    bool run(const MSeq& root, size_t pos, size_t k, size_t& end) {
        const size_t choice_base = choices_.size(), trail_base = trail_.size();
        Mode mode = Mode::Eval;
        const MSeq* q = &root;
        size_t i = 0;
        while (true) {
            if (mode == Mode::Eval) {
                mode = eval(q, i, pos, k);
            } else if (mode == Mode::Return) {
                const Frame f = frames_[k];
                switch (f.kind) {
                    case Frame::Accept:
                        if (require_progress_ && pos == start_) {
                            mode = Mode::Fail;
                            break;
                        }
                        [[fallthrough]];
                    case Frame::SubEnd:
                        end = pos;
                        choices_.resize(choice_base);
                        return true;
                    case Frame::Seq:
                        q = f.q;
                        i = f.i;
                        k = f.parent;
                        mode = Mode::Eval;
                        break;
                    case Frame::Close:
                        set_mark(f.mark, static_cast<long>(pos));
                        k = f.parent;
                        break;
                    case Frame::Rep:
                        mode = repeat(*f.n, f.count, pos, f.last, f.parent, q, i, k);
                        break;
                }
            } else {
                if (choices_.size() == choice_base) {
                    undo(trail_base);
                    return false;
                }
                const Choice c = choices_.back();
                choices_.pop_back();
                undo(c.trail);
                frames_.resize(c.frames);
                pos = c.pos;
                k = c.k;
                switch (c.kind) {
                    case Choice::Branch:
                        if (c.alt + 1 < c.n->kids.size()) {
                            Choice next = c;
                            ++next.alt;
                            push_choice(next);
                        }
                        q = &c.n->kids[c.alt];
                        i = 0;
                        mode = Mode::Eval;
                        break;
                    case Choice::RepeatSkip:
                        mode = Mode::Return;
                        break;
                    case Choice::RepeatMore: {
                        const bool more = c.count < c.n->max || c.n->max == kMaxRepeat;
                        if (!more || (c.last >= 0 && pos == static_cast<size_t>(c.last))) {
                            mode = Mode::Fail;
                            break;
                        }
                        k = push_frame({Frame::Rep, nullptr, 0, 0, c.n, c.count + 1, static_cast<long>(pos), c.k});
                        q = &c.n->kids[0];
                        i = 0;
                        mode = Mode::Eval;
                        break;
                    }
                }
            }
        }
    }

    // Run q[i:] from pos with continuation k up to its first node that is not
    // a single character or an anchor, and start that node.
    Mode eval(const MSeq*& q, size_t& i, size_t& pos, size_t& k) {
        while (i < q->size()) {
            const MNode& n = (*q)[i];
            if (n.kind == MNode::Lit || n.kind == MNode::NotLit || n.kind == MNode::Any || n.kind == MNode::In) {
                if (!char_at(n, pos)) return Mode::Fail;
                ++pos;
                ++i;
            } else if (n.kind == MNode::At) {
                if (!at(n, pos)) return Mode::Fail;
                ++i;
            } else {
                break;
            }
        }
        if (i == q->size()) return Mode::Return;
        const MNode& n = (*q)[i];
        const size_t next = push_frame({Frame::Seq, q, i + 1, 0, nullptr, 0, -1, k});
        k = next;
        switch (n.kind) {
            case MNode::Branch:
                if (n.kids.empty()) return Mode::Fail;
                if (n.kids.size() > 1) push_choice({Choice::Branch, &n, 1, pos, next, 0, -1, 0, 0});
                q = &n.kids[0];
                i = 0;
                return Mode::Eval;
            case MNode::Mark: {
                const size_t b = static_cast<size_t>(n.group) * 2;
                set_mark(b, static_cast<long>(pos));
                k = push_frame({Frame::Close, nullptr, 0, b + 1, nullptr, 0, -1, next});
                q = &n.kids[0];
                i = 0;
                return Mode::Eval;
            }
            case MNode::Repeat:
                if (n.rep == RepKind::Possessive) return possessive(n, pos) ? Mode::Return : Mode::Fail;
                return repeat(n, 0, pos, -1, next, q, i, k);
            case MNode::Ref: {
                if (!group_set(n.group)) return Mode::Fail;
                const size_t b = static_cast<size_t>(marks_[static_cast<size_t>(n.group) * 2]);
                const size_t e = static_cast<size_t>(marks_[static_cast<size_t>(n.group) * 2 + 1]);
                if (pos + (e - b) > s_.size()) return Mode::Fail;
                for (size_t j = 0; j < e - b; ++j) {
                    char32_t x = s_[b + j], y = s_[pos + j];
                    if (n.refmode == 1) {
                        x = detail::sre_lower(x);
                        y = detail::sre_lower(y);
                    } else if (n.refmode == 2) {
                        x = ascii_lower(x);
                        y = ascii_lower(y);
                    }
                    if (x != y) return Mode::Fail;
                }
                pos += e - b;
                return Mode::Return;
            }
            case MNode::RefExists:
                if (group_set(n.group) || n.has_no) {
                    q = &n.kids[group_set(n.group) ? 0 : 1];
                    i = 0;
                    return Mode::Eval;
                }
                return Mode::Return;
            case MNode::Look: {
                const size_t trail = trail_.size();
                bool matched = false;
                if (!n.behind || pos >= n.width) {
                    const size_t from = n.behind ? pos - static_cast<size_t>(n.width) : pos;
                    const size_t frames = frames_.size();
                    size_t ignored = 0;
                    matched = run(n.kids[0], from, push_frame({Frame::SubEnd}), ignored);
                    frames_.resize(frames);  // no backtrack point refers to the frames of the sub-match
                }
                if (n.negative) {
                    undo(trail);
                    return matched ? Mode::Fail : Mode::Return;
                }
                return matched ? Mode::Return : Mode::Fail;
            }
            case MNode::Atomic: {
                const size_t frames = frames_.size();
                size_t end = 0;
                const bool matched = run(n.kids[0], pos, push_frame({Frame::SubEnd}), end);
                frames_.resize(frames);
                if (!matched) return Mode::Fail;
                pos = end;
                return Mode::Return;
            }
            default:
                return Mode::Fail;
        }
    }

    // sre's REPEAT with MAX_UNTIL or MIN_UNTIL, after `count` iterations that
    // ended at `pos`; `last` is where the latest optional iteration began.
    Mode repeat(const MNode& n, uint64_t count, size_t pos, long last, size_t next, const MSeq*& q, size_t& i,
                size_t& k) {
        if (count < n.min) {
            k = push_frame({Frame::Rep, nullptr, 0, 0, &n, count + 1, last, next});
            q = &n.kids[0];
            i = 0;
            return Mode::Eval;
        }
        const bool more = count < n.max || n.max == kMaxRepeat;
        if (n.rep == RepKind::Max) {
            if (more && (last < 0 || pos != static_cast<size_t>(last))) {
                push_choice({Choice::RepeatSkip, &n, 0, pos, next, 0, -1, 0, 0});
                k = push_frame({Frame::Rep, nullptr, 0, 0, &n, count + 1, static_cast<long>(pos), next});
                q = &n.kids[0];
                i = 0;
                return Mode::Eval;
            }
            k = next;
            return Mode::Return;
        }
        push_choice({Choice::RepeatMore, &n, 0, pos, next, count, last, 0, 0});
        k = next;
        return Mode::Return;
    }

    // A possessive repeat: as many iterations as match, without backtracking
    // into them. Leaves pos after the last iteration.
    bool possessive(const MNode& n, size_t& pos) {
        const size_t before = trail_.size();
        const MSeq& body = n.kids[0];
        uint64_t count = 0;
        auto once = [&](size_t from, size_t& to) {
            const size_t frames = frames_.size();
            const bool matched = run(body, from, push_frame({Frame::SubEnd}), to);
            frames_.resize(frames);
            return matched;
        };
        while (count < n.min) {
            size_t to = 0;
            if (!once(pos, to)) {
                undo(before);
                return false;
            }
            pos = to;
            ++count;
        }
        long prev = -1;
        while ((count < n.max || n.max == kMaxRepeat) && (prev < 0 || pos != static_cast<size_t>(prev))) {
            prev = static_cast<long>(pos);
            size_t to = 0;
            if (!once(pos, to)) break;
            pos = to;
            ++count;
        }
        return true;
    }

    bool char_at(const MNode& n, size_t pos) const {
        if (pos >= s_.size()) return false;
        const char32_t ch = s_[pos];
        switch (n.kind) {
            case MNode::Lit:
            case MNode::NotLit: {
                bool eq;
                switch (n.cmode) {
                    case 0: eq = ch == n.c; break;
                    case 1: eq = detail::sre_lower(ch) == n.c; break;
                    case 2: eq = ascii_lower(ch) == n.c; break;
                    default: {
                        const char32_t lo = detail::sre_lower(ch);
                        eq = std::find(n.cset.begin(), n.cset.end(), lo) != n.cset.end();
                    }
                }
                return n.kind == MNode::Lit ? eq : !eq;
            }
            case MNode::Any: return n.dotall || ch != U'\n';
            case MNode::In: return charset_has(*n.charset, ch);
            default: return false;
        }
    }

    bool word_at(size_t i, bool ascii) const { return category(Cat::Word, s_[i], ascii); }

    bool at(const MNode& n, size_t pos) const {
        const size_t len = s_.size();
        switch (n.at) {
            case AtCode::Beginning:
                return pos == 0 || (n.multiline && s_[pos - 1] == U'\n');
            case AtCode::BeginningString:
                return pos == 0;
            case AtCode::End:
                if (n.multiline) return pos == len || s_[pos] == U'\n';
                return pos == len || (pos + 1 == len && s_[pos] == U'\n');
            case AtCode::EndString:
                return pos == len;
            case AtCode::Boundary:
            case AtCode::NonBoundary: {
                if (len == 0) return false;
                const bool that = pos > 0 && word_at(pos - 1, n.ascii);
                const bool thiss = pos < len && word_at(pos, n.ascii);
                return n.at == AtCode::Boundary ? thiss != that : thiss == that;
            }
        }
        return false;
    }

    bool group_set(int g) const {
        const long b = marks_[static_cast<size_t>(g) * 2], e = marks_[static_cast<size_t>(g) * 2 + 1];
        return b >= 0 && e >= 0 && e >= b;
    }

    const Program& program_;
    U32View s_;
    std::vector<long> marks_;
    std::vector<Frame> frames_;
    std::vector<Choice> choices_;
    std::vector<std::pair<size_t, long>> trail_;
    size_t start_ = 0;
    bool require_progress_ = false;
};

// A parsed replacement template: literal pieces and group indexes.
struct Template {
    std::vector<U32> literals;  // literals.size() == groups.size() + 1
    std::vector<int> groups;
};

Template parse_template(U32View repl, const Program& program, const std::map<U32, int>& groupindex) {
    Tokenizer s(repl);
    Template out;
    U32 literal;
    auto addgroup = [&](uint64_t index, size_t pos) {
        if (index > static_cast<uint64_t>(program.groups - 1)) s.error("invalid group reference " + std::to_string(index), pos);
        out.literals.push_back(literal);
        literal.clear();
        out.groups.push_back(static_cast<int>(index));
    };
    while (true) {
        Token t = s.get();
        if (!t) break;
        U32 thiss = *t;
        if (thiss[0] == U'\\' && thiss.size() == 2) {
            const char32_t c = thiss[1];
            if (c == U'g') {
                if (!s.match(U"<")) s.error("missing <");
                const U32 name = s.getuntil(U'>', "group name");
                const bool numeric = std::all_of(name.begin(), name.end(), is_ascii_digit);
                uint64_t index;
                if (!numeric) {
                    s.checkgroupname(name, 1);
                    const auto it = groupindex.find(name);
                    if (it == groupindex.end()) throw IndexError("unknown group name " + detail::py_str_repr(name));
                    index = static_cast<uint64_t>(it->second);
                } else {
                    index = parse_uint(name, 10);
                    if (index >= kMaxGroups) s.error("invalid group reference " + decimal(name), name.size() + 1);
                }
                addgroup(index, name.size() + 1);
            } else if (c == U'0') {
                if (is_oct_token(s.next())) {
                    thiss += *s.get();
                    if (is_oct_token(s.next())) thiss += *s.get();
                }
                literal += static_cast<char32_t>(parse_uint(thiss.substr(1), 8) & 0xFF);
            } else if (is_ascii_digit(c)) {
                bool isoctal = false;
                if (is_digits_token(s.next())) {
                    thiss += *s.get();
                    if (is_oct_digit(c) && is_oct_digit(thiss[2]) && is_oct_token(s.next())) {
                        thiss += *s.get();
                        isoctal = true;
                        const uint64_t v = parse_uint(thiss.substr(1), 8);
                        if (v > 0377) s.error("octal escape value " + str(thiss) + " outside of range 0-0o377", thiss.size());
                        literal += static_cast<char32_t>(v);
                    }
                }
                if (!isoctal) addgroup(parse_uint(thiss.substr(1), 10), thiss.size() - 1);
            } else if (auto code = escapes(thiss)) {
                literal += code->value;
            } else {
                if (is_ascii_letter(c)) s.error("bad escape " + str(thiss), thiss.size());
                literal += thiss;
            }
        } else {
            literal += thiss;
        }
    }
    out.literals.push_back(literal);
    return out;
}

}  // namespace impl

using namespace impl;

// The group names are kept beside the program for the replacement templates.
struct ProgramWithNames : Program {
    std::map<U32, int> groupindex;
};

Pattern::Pattern(U32View pattern, int flags) {
    if (flags & LOCALE) throw ValueError("cannot use LOCALE flag with a str pattern");
    State state;
    state.flags = flags;
    Tokenizer source(pattern);
    Seq p = parse(source, state, flags & VERBOSE);
    // fix_flags
    int final_flags = state.flags;
    if (final_flags & LOCALE) throw ValueError("cannot use LOCALE flag with a str pattern");
    if (!(final_flags & ASCII)) {
        final_flags |= UNICODE;
    } else if (final_flags & UNICODE) {
        throw ValueError("ASCII and UNICODE flags are incompatible");
    }
    if (source.next()) source.error("unbalanced parenthesis");
    for (const auto& [g, pos] : state.grouprefpos) {
        if (g >= static_cast<uint64_t>(state.groups()))
            raise_at("invalid group reference " + std::to_string(g), pattern, static_cast<std::ptrdiff_t>(pos));
    }
    auto program = std::make_shared<ProgramWithNames>();
    program->code = compile_seq(p, final_flags, state);
    program->groups = state.groups();
    program->groupindex = state.groupdict;
    program_ = program;
}

int Pattern::groups() const { return program_->groups - 1; }

std::vector<std::pair<size_t, size_t>> Pattern::finditer_spans(U32View s) const {
    std::vector<std::pair<size_t, size_t>> out;
    Matcher m(*program_, s);
    size_t pos = 0;
    bool must_advance = false;
    while (pos <= s.size() && m.search(pos, must_advance)) {
        const size_t b = static_cast<size_t>(m.marks()[0]), e = static_cast<size_t>(m.marks()[1]);
        out.emplace_back(b, e);
        must_advance = e == b;
        pos = e;
    }
    return out;
}

U32 Pattern::sub(U32View repl, U32View s, size_t count) const {
    const auto& named = static_cast<const ProgramWithNames&>(*program_);
    const Template tpl = parse_template(repl, *program_, named.groupindex);
    Matcher m(*program_, s);
    U32 out;
    size_t i = 0, pos = 0, n = 0;
    bool must_advance = false;
    while ((count == 0 || n < count) && pos <= s.size() && m.search(pos, must_advance)) {
        const auto& marks = m.marks();
        const size_t b = static_cast<size_t>(marks[0]), e = static_cast<size_t>(marks[1]);
        if (i < b) out.append(s.substr(i, b - i));
        for (size_t k = 0; k < tpl.groups.size(); ++k) {
            out += tpl.literals[k];
            const size_t g = static_cast<size_t>(tpl.groups[k]);
            const long gb = marks[g * 2], ge = marks[g * 2 + 1];
            if (gb >= 0 && ge >= 0 && ge >= gb)
                out.append(s.substr(static_cast<size_t>(gb), static_cast<size_t>(ge - gb)));
        }
        out += tpl.literals.back();
        i = e;
        ++n;
        must_advance = e == b;
        pos = e;
    }
    if (i < s.size()) out.append(s.substr(i));
    return out;
}

U32 escape(U32View pattern) {
    static const U32View special = U"()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    U32 out;
    for (char32_t c : pattern) {
        if (special.find(c) != U32View::npos) out += U'\\';
        out += c;
    }
    return out;
}

}  // namespace tugaphone::pyre
