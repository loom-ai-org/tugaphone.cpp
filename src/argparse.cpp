#include "argparse.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>

#include <sys/ioctl.h>
#include <unistd.h>

#include "pyio.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::detail::argparse {

namespace {

// ── textwrap ────────────────────────────────────────────────────────────────

// textwrap._whitespace.
bool tw_space(char32_t c) { return c == U'\t' || c == U'\n' || c == U'\v' || c == U'\f' || c == U'\r' || c == U' '; }
// [^\d\W]: a word character that is not a decimal digit.
bool tw_letter(char32_t c) { return is_word(c) && !is_decimal(c); }
// [\w!"'&.,?]
bool tw_word_punct(char32_t c) {
    return is_word(c) || c == U'!' || c == U'"' || c == U'\'' || c == U'&' || c == U'.' || c == U',' || c == U'?';
}

// text.expandtabs(8), then each textwrap whitespace character as a space.
U32 munge_whitespace(U32View text) {
    U32 out;
    size_t column = 0;
    for (char32_t c : text) {
        if (c == U'\t') {
            const size_t n = 8 - column % 8;
            out.append(n, U' ');
            column += n;
        } else {
            out.push_back(tw_space(c) ? U' ' : c);
            column = (c == U'\n' || c == U'\r') ? 0 : column + 1;
        }
    }
    return out;
}

// The length of the run of '-' at `at`.
size_t dash_run(U32View s, size_t at) {
    size_t n = 0;
    while (at + n < s.size() && s[at + n] == U'-') ++n;
    return n;
}

// (?<=wp) -{2,} (?=\w) at position `at`: the end of the dashes, or 0.
size_t em_dash_end(U32View s, size_t at) {
    if (at == 0 || !tw_word_punct(s[at - 1])) return 0;
    const size_t n = dash_run(s, at);
    if (n < 2 || at + n >= s.size() || !is_word(s[at + n])) return 0;
    return at + n;
}

// TextWrapper.wordsep_re.split(text), without the empty strings. Every
// position starts a match, so the chunks are the successive matches.
std::vector<U32> split_chunks(U32View s) {
    std::vector<U32> chunks;
    size_t p = 0;
    while (p < s.size()) {
        size_t end = 0;
        if (tw_space(s[p])) {
            end = p;
            while (end < s.size() && tw_space(s[end])) ++end;
        } else if (const size_t dash_end = em_dash_end(s, p)) {
            end = dash_end;
        } else {
            // %(nws)s+? then: a hyphen inside a word, the end of the word, or an em-dash.
            for (size_t k = p + 1;; ++k) {
                if (k < s.size() && s[k] == U'-') {
                    const bool behind = (k >= 2 && tw_letter(s[k - 2]) && tw_letter(s[k - 1])) ||
                                        (k >= 3 && tw_letter(s[k - 3]) && s[k - 2] == U'-' && tw_letter(s[k - 1]));
                    const bool ahead = k + 2 < s.size() && tw_letter(s[k + 1]) &&
                                       ((s[k + 2] == U'-' && k + 3 < s.size() && tw_letter(s[k + 3])) ||
                                        tw_letter(s[k + 2]));
                    if (behind && ahead) {
                        end = k + 1;
                        break;
                    }
                }
                if (k == s.size() || tw_space(s[k])) {
                    end = k;
                    break;
                }
                if (tw_word_punct(s[k - 1])) {
                    const size_t n = dash_run(s, k);
                    if (n >= 2 && k + n < s.size() && is_word(s[k + n])) {
                        end = k;
                        break;
                    }
                }
            }
        }
        chunks.emplace_back(s.substr(p, end - p));
        p = end;
    }
    return chunks;
}

bool blank(const U32& chunk) { return strip_ws(chunk).empty(); }

// TextWrapper._handle_long_word.
void handle_long_word(std::vector<U32>& reversed_chunks, std::vector<U32>& cur_line, long cur_len, long width) {
    const long space_left = width < 1 ? 1 : width - cur_len;
    U32& chunk = reversed_chunks.back();
    size_t end = static_cast<size_t>(space_left);
    if (static_cast<long>(chunk.size()) > space_left) {
        const size_t limit = std::min(chunk.size(), static_cast<size_t>(space_left));
        const size_t hyphen = limit == 0 ? U32::npos : chunk.rfind(U'-', limit - 1);
        if (hyphen != U32::npos && hyphen > 0 &&
            std::any_of(chunk.begin(), chunk.begin() + static_cast<long>(hyphen), [](char32_t c) { return c != U'-'; }))
            end = hyphen + 1;
    }
    end = std::min(end, chunk.size());
    cur_line.push_back(chunk.substr(0, end));
    chunk = chunk.substr(end);
}

// ── argparse helpers ────────────────────────────────────────────────────────

U32 repr(const U32& s) { return u(py_str_repr(s)); }

U32 join(const std::vector<U32>& parts, U32View sep) {
    U32 out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// '%*s' % (n, ''): n spaces.
U32 spaces(long n) { return n > 0 ? U32(static_cast<size_t>(n), U' ') : U32(); }

// '%-*s' % (width, s).
U32 ljust(const U32& s, long width) {
    return static_cast<long>(s.size()) >= width ? s : s + spaces(width - static_cast<long>(s.size()));
}

// ^-\d+$|^-\d*\.\d+$ ($ also matches before a final newline).
bool negative_number(const U32& s) {
    U32View v = s;
    if (!v.empty() && v.back() == U'\n') v.remove_suffix(1);
    if (v.size() < 2 || v[0] != U'-') return false;
    v.remove_prefix(1);
    const size_t dot = v.find(U'.');
    auto digits = [](U32View d) { return std::all_of(d.begin(), d.end(), is_decimal); };
    if (dot == U32View::npos) return digits(v);
    return digits(v.substr(0, dot)) && dot + 1 < v.size() && digits(v.substr(dot + 1));
}

// One element of an nargs pattern: a set of pattern letters, repeated.
struct Atom {
    U32 letters;
    size_t min, max;
};
constexpr size_t kMany = static_cast<size_t>(-1);

// _get_nargs_pattern(action) as atoms.
std::vector<Atom> nargs_atoms(const Action& action) {
    const bool optional = !action.option_strings.empty();
    switch (action.nargs) {
        case Action::Nargs::One:
            if (optional) return {{U"A", 1, 1}};
            return {{U"-", 0, kMany}, {U"A", 1, 1}, {U"-", 0, kMany}};
        case Action::Nargs::Zero:
            if (optional) return {};
            return {{U"-", 0, kMany}};
        case Action::Nargs::Parser:
            if (optional) return {{U"A", 1, 1}, {U"AO", 0, kMany}};
            return {{U"-", 0, kMany}, {U"A", 1, 1}, {U"-AO", 0, kMany}};
    }
    return {};
}

// re.match of the concatenated groups of `actions` at the start of `pattern`:
// the length each group matched and the end of the match, or nullopt. The
// quantifiers are greedy and backtrack, as in the re engine.
struct GroupMatch {
    std::vector<size_t> lengths;
    size_t end = 0;
};
std::optional<GroupMatch> match_groups(const std::vector<const Action*>& actions, const U32& pattern) {
    std::vector<std::pair<Atom, size_t>> atoms;  // atom, group
    for (size_t g = 0; g < actions.size(); ++g)
        for (const Atom& a : nargs_atoms(*actions[g])) atoms.emplace_back(a, g);
    std::vector<size_t> taken(atoms.size());
    std::function<bool(size_t, size_t)> step = [&](size_t i, size_t pos) -> bool {
        if (i == atoms.size()) return true;
        const Atom& a = atoms[i].first;
        size_t n = 0;
        while (n < a.max && pos + n < pattern.size() && a.letters.find(pattern[pos + n]) != U32::npos) ++n;
        for (size_t take = n + 1; take-- > a.min;) {
            taken[i] = take;
            if (step(i + 1, pos + take)) return true;
        }
        return false;
    };
    if (!step(0, 0)) return std::nullopt;
    GroupMatch m;
    m.lengths.assign(actions.size(), 0);
    for (size_t i = 0; i < atoms.size(); ++i) {
        m.lengths[atoms[i].second] += taken[i];
        m.end += taken[i];
    }
    return m;
}

}  // namespace

std::vector<U32> wrap(U32View text, long width, U32View initial_indent, U32View subsequent_indent) {
    if (width <= 0) throw ValueError("invalid width " + std::to_string(width) + " (must be > 0)");
    std::vector<U32> chunks = split_chunks(munge_whitespace(text));
    std::reverse(chunks.begin(), chunks.end());
    std::vector<U32> lines;
    while (!chunks.empty()) {
        std::vector<U32> cur_line;
        long cur_len = 0;
        const U32 indent(lines.empty() ? initial_indent : subsequent_indent);
        const long line_width = width - static_cast<long>(indent.size());
        if (blank(chunks.back()) && !lines.empty()) chunks.pop_back();
        while (!chunks.empty()) {
            const long l = static_cast<long>(chunks.back().size());
            if (cur_len + l > line_width) break;
            cur_line.push_back(chunks.back());
            chunks.pop_back();
            cur_len += l;
        }
        if (!chunks.empty() && static_cast<long>(chunks.back().size()) > line_width) {
            handle_long_word(chunks, cur_line, cur_len, line_width);
            cur_len = 0;
            for (const U32& c : cur_line) cur_len += static_cast<long>(c.size());
        }
        if (!cur_line.empty() && blank(cur_line.back())) {
            cur_len -= static_cast<long>(cur_line.back().size());
            cur_line.pop_back();
        }
        if (!cur_line.empty()) {
            U32 line = indent;
            for (const U32& c : cur_line) line += c;
            lines.push_back(line);
        }
    }
    return lines;
}

U32 fill(U32View text, long width, U32View initial_indent, U32View subsequent_indent) {
    return join(wrap(text, width, initial_indent, subsequent_indent), U"\n");
}

long terminal_columns() {
    long columns = 0;
    if (const char* env = std::getenv("COLUMNS")) {
        if (const auto value = py_int(env)) {
            try {
                columns = std::stol(value->str());
            } catch (const std::out_of_range&) {
                // A Python int has no range: a width this large never wraps,
                // and a negative one is no width.
                columns = value->str()[0] == '-' ? 0 : std::numeric_limits<long>::max() / 4;
            }
        }
    }
    if (columns > 0) return columns;
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) return size.ws_col;
    return 80;
}

std::optional<U32> action_name(const Action& action) {
    if (!action.option_strings.empty()) return join(action.option_strings, U"/");
    if (action.metavar) return action.metavar;
    if (!action.dest.empty()) return action.dest;
    if (action.choices && !action.choices->empty()) return U"{" + join(*action.choices, U",") + U"}";
    return std::nullopt;
}

// ── help formatter ──────────────────────────────────────────────────────────

namespace {

class Formatter {
public:
    Formatter(U32 prog, long width)
        : prog_(std::move(prog)), width_(width - 2), max_help_position_(std::min(24L, std::max(width_ - 20, 4L))) {}

    U32 format_usage(const std::vector<const Action*>& actions) const {
        const U32 prefix = U"usage: ";
        U32 usage;
        if (actions.empty()) {
            usage = prog_;
        } else {
            std::vector<const Action*> optionals, positionals;
            for (const Action* a : actions) (a->option_strings.empty() ? positionals : optionals).push_back(a);
            std::vector<const Action*> all = optionals;
            all.insert(all.end(), positionals.begin(), positionals.end());
            const U32 action_usage = actions_usage(all);
            usage = action_usage.empty() ? prog_ : prog_ + U" " + action_usage;
            const long text_width = width_ - indent_;
            if (static_cast<long>(prefix.size() + usage.size()) > text_width) {
                const std::vector<U32> opt_parts = usage_parts(actions_usage(optionals));
                const std::vector<U32> pos_parts = usage_parts(actions_usage(positionals));
                auto get_lines = [&](const std::vector<U32>& parts, const U32& indent, const U32* line_prefix) {
                    std::vector<U32> lines, line;
                    long line_len = static_cast<long>(line_prefix ? line_prefix->size() : indent.size()) - 1;
                    for (const U32& part : parts) {
                        if (line_len + 1 + static_cast<long>(part.size()) > text_width && !line.empty()) {
                            lines.push_back(indent + join(line, U" "));
                            line.clear();
                            line_len = static_cast<long>(indent.size()) - 1;
                        }
                        line.push_back(part);
                        line_len += static_cast<long>(part.size()) + 1;
                    }
                    if (!line.empty()) lines.push_back(indent + join(line, U" "));
                    if (line_prefix) lines[0] = lines[0].substr(indent.size());
                    return lines;
                };
                std::vector<U32> lines;
                if (4 * static_cast<long>(prefix.size() + prog_.size()) <= 3 * text_width) {
                    const U32 indent = spaces(static_cast<long>(prefix.size() + prog_.size() + 1));
                    std::vector<U32> first{prog_};
                    if (!opt_parts.empty()) {
                        first.insert(first.end(), opt_parts.begin(), opt_parts.end());
                        lines = get_lines(first, indent, &prefix);
                        const auto more = get_lines(pos_parts, indent, nullptr);
                        lines.insert(lines.end(), more.begin(), more.end());
                    } else if (!pos_parts.empty()) {
                        first.insert(first.end(), pos_parts.begin(), pos_parts.end());
                        lines = get_lines(first, indent, &prefix);
                    } else {
                        lines = {prog_};
                    }
                } else {
                    const U32 indent = spaces(static_cast<long>(prefix.size()));
                    std::vector<U32> parts = opt_parts;
                    parts.insert(parts.end(), pos_parts.begin(), pos_parts.end());
                    lines = get_lines(parts, indent, nullptr);
                    if (lines.size() > 1) {
                        lines = get_lines(opt_parts, indent, nullptr);
                        const auto more = get_lines(pos_parts, indent, nullptr);
                        lines.insert(lines.end(), more.begin(), more.end());
                    }
                    lines.insert(lines.begin(), prog_);
                }
                usage = join(lines, U"\n");
            }
        }
        return prefix + usage + U"\n\n";
    }

    U32 format_text(const U32& text) const {
        U32 t = text;
        if (t.find(U"%(prog)") != U32::npos) t = replace(replace(t, U"%(prog)s", prog_), U"%%", U"%");
        const long text_width = std::max(width_ - indent_, 11L);
        const U32 indent = spaces(indent_);
        return fill(collapse(t), text_width, indent, indent) + U"\n\n";
    }

    // add_argument: the longest invocation decides the help column.
    void add_argument(const Action& action) {
        long longest = static_cast<long>(invocation(action).size()) + indent_;
        indent_ += 2;
        for (const auto& sub : action.choice_actions)
            longest = std::max(longest, static_cast<long>(invocation(*sub).size()) + indent_);
        indent_ -= 2;
        action_max_length_ = std::max(action_max_length_, longest);
    }

    U32 format_action(const Action& action) {
        const long help_position = std::min(action_max_length_ + 2, max_help_position_);
        const long help_width = std::max(width_ - help_position, 11L);
        const long action_width = help_position - indent_ - 2;
        U32 header = invocation(action);
        long indent_first = 0;
        if (!action.help || action.help->empty()) {
            header = spaces(indent_) + header + U"\n";
        } else if (static_cast<long>(header.size()) <= action_width) {
            header = spaces(indent_) + ljust(header, action_width) + U"  ";
        } else {
            header = spaces(indent_) + header + U"\n";
            indent_first = help_position;
        }
        U32 out = header;
        if (action.help && !strip_ws(*action.help).empty()) {
            const std::vector<U32> lines = wrap(collapse(*action.help), help_width);
            if (!lines.empty()) {
                out += spaces(indent_first) + lines[0] + U"\n";
                for (size_t i = 1; i < lines.size(); ++i) out += spaces(help_position) + lines[i] + U"\n";
            }
        } else if (header.empty() || header.back() != U'\n') {
            out += U"\n";
        }
        indent_ += 2;
        for (const auto& sub : action.choice_actions) out += format_action(*sub);
        indent_ -= 2;
        return out;
    }

    // One _Section: the heading, then the actions one level in.
    U32 format_section(const U32& heading, const std::vector<const Action*>& actions) {
        indent_ += 2;
        U32 items;
        for (const Action* a : actions) items += format_action(*a);
        indent_ -= 2;
        if (items.empty()) return U"";
        return U"\n" + spaces(indent_) + heading + U":\n" + items + U"\n";
    }

    void indent() { indent_ += 2; }
    void dedent() { indent_ -= 2; }

    static U32 finish(U32 help) {
        // \n\n\n+ -> \n\n, then strip('\n') + '\n'.
        U32 out;
        for (size_t i = 0; i < help.size();) {
            if (help[i] == U'\n') {
                size_t j = i;
                while (j < help.size() && help[j] == U'\n') ++j;
                out.append(j - i >= 3 ? 2 : j - i, U'\n');
                i = j;
            } else {
                out.push_back(help[i++]);
            }
        }
        if (out.empty()) return out;
        const size_t a = out.find_first_not_of(U'\n');
        if (a == U32::npos) return U"\n";
        const size_t b = out.find_last_not_of(U'\n');
        return out.substr(a, b - a + 1) + U"\n";
    }

private:
    // _whitespace_matcher.sub(' ', text).strip(): ASCII whitespace runs.
    static U32 collapse(const U32& text) {
        U32 out;
        bool in_space = false;
        for (char32_t c : text) {
            const bool ws = c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v';
            if (ws) {
                if (!in_space) out.push_back(U' ');
                in_space = true;
            } else {
                out.push_back(c);
                in_space = false;
            }
        }
        return strip_ws(out);
    }

    static U32 metavar(const Action& action, const U32& default_metavar) {
        if (action.metavar) return *action.metavar;
        if (action.choices) return U"{" + join(*action.choices, U",") + U"}";
        return default_metavar;
    }
    static U32 default_metavar(const Action& action) {
        return action.option_strings.empty() ? action.dest : upper(action.dest);
    }
    static U32 format_args(const Action& action, const U32& default_mv) {
        const U32 mv = metavar(action, default_mv);
        if (action.nargs == Action::Nargs::Parser) return mv + U" ...";
        return mv;
    }
    static U32 invocation(const Action& action) {
        if (action.option_strings.empty()) return metavar(action, action.dest);
        if (action.nargs == Action::Nargs::Zero) return join(action.option_strings, U", ");
        const U32 args = format_args(action, default_metavar(action));
        std::vector<U32> parts;
        for (const U32& o : action.option_strings) parts.push_back(o + U" " + args);
        return join(parts, U", ");
    }

    static U32 actions_usage(const std::vector<const Action*>& actions) {
        std::vector<U32> parts;
        for (const Action* a : actions) {
            if (a->option_strings.empty()) {
                parts.push_back(format_args(*a, a->dest));
                continue;
            }
            U32 part = a->nargs == Action::Nargs::Zero
                           ? a->option_strings[0]
                           : a->option_strings[0] + U" " + format_args(*a, default_metavar(*a));
            if (!a->required) part = U"[" + part + U"]";
            parts.push_back(part);
        }
        U32 text = join(parts, U" ");
        auto is_open = [](char32_t c) { return c == U'[' || c == U'('; };
        auto is_close = [](char32_t c) { return c == U']' || c == U')'; };
        // re.sub(r'([\[(]) ', r'\1', text)
        U32 t1;
        for (size_t i = 0; i < text.size(); ++i) {
            t1.push_back(text[i]);
            if (is_open(text[i]) && i + 1 < text.size() && text[i + 1] == U' ') ++i;
        }
        // re.sub(r' ([\])])', r'\1', text)
        U32 t2;
        for (size_t i = 0; i < t1.size(); ++i) {
            if (t1[i] == U' ' && i + 1 < t1.size() && is_close(t1[i + 1])) continue;
            t2.push_back(t1[i]);
        }
        // re.sub(r'[\[(] *[\])]', r'', text)
        U32 t3;
        for (size_t i = 0; i < t2.size(); ++i) {
            if (is_open(t2[i])) {
                size_t j = i + 1;
                while (j < t2.size() && t2[j] == U' ') ++j;
                if (j < t2.size() && is_close(t2[j])) {
                    i = j;
                    continue;
                }
            }
            t3.push_back(t2[i]);
        }
        return strip_ws(t3);
    }

    // re.findall(r'\(.*?\)+(?=\s|$)|\[.*?\]+(?=\s|$)|\S+', text)
    static std::vector<U32> usage_parts(const U32& text) {
        std::vector<U32> parts;
        auto at_boundary = [&](size_t k) {
            return k == text.size() || is_space(text[k]) || (k + 1 == text.size() && text[k] == U'\n');
        };
        size_t p = 0;
        while (p < text.size()) {
            size_t end = 0;
            for (const auto& [open, close] : {std::pair<char32_t, char32_t>{U'(', U')'}, {U'[', U']'}}) {
                if (text[p] != open) continue;
                for (size_t k = p + 1; k < text.size() && text[k] != U'\n' && !end; ++k) {
                    if (text[k] != close) continue;
                    size_t m = k;
                    while (m < text.size() && text[m] == close) ++m;
                    for (size_t stop = m; stop > k && !end; --stop)
                        if (at_boundary(stop)) end = stop;
                }
                if (end) break;
            }
            if (!end && !is_space(text[p])) {
                end = p;
                while (end < text.size() && !is_space(text[end])) ++end;
            }
            if (end) {
                parts.push_back(text.substr(p, end - p));
                p = end;
            } else {
                ++p;
            }
        }
        return parts;
    }

    U32 prog_;
    long width_;
    long max_help_position_;
    long indent_ = 0;
    long action_max_length_ = 0;
};

}  // namespace

// ── parser ──────────────────────────────────────────────────────────────────

U32 Parser::ArgumentError::str() const {
    if (!argument_name) return message;
    return U"argument " + *argument_name + U": " + message;
}

Parser::Parser(U32 prog, std::optional<U32> description, bool add_help)
    : prog_(std::move(prog)), description_(std::move(description)) {
    if (add_help) {
        Action help;
        help.kind = Action::Kind::Help;
        help.nargs = Action::Nargs::Zero;
        help.dest = U"help";
        help.help = U"show this help message and exit";
        add_argument({U"-h", U"--help"}, std::move(help));
    }
}

Action& Parser::add_argument(std::vector<U32> names, Action spec) {
    auto action = std::make_unique<Action>(std::move(spec));
    if (!names.empty() && !names[0].empty() && names[0][0] == U'-') {
        action->option_strings = names;
        if (action->dest.empty()) {
            U32 dest = names[0];
            for (const U32& n : names) {
                if (n.size() > 1 && n[1] == U'-') {
                    dest = n;
                    break;
                }
            }
            dest = lstrip(dest, U"-");
            std::replace(dest.begin(), dest.end(), U'-', U'_');
            action->dest = dest;
        }
        for (const U32& n : names) option_string_actions_.emplace_back(n, action.get());
    } else {
        action->dest = names.at(0);
        action->required = action->nargs != Action::Nargs::Zero;
    }
    if (action->kind == Action::Kind::StoreTrue) {
        action->nargs = Action::Nargs::Zero;
        action->default_value = U"False";
    }
    actions_.push_back(std::move(action));
    return *actions_.back();
}

Action& Parser::add_subparsers(U32 dest, bool required) {
    Action spec;
    spec.kind = Action::Kind::SubParsers;
    spec.nargs = Action::Nargs::Parser;
    spec.required = required;
    spec.choices.emplace();
    auto action = std::make_unique<Action>(std::move(spec));
    action->dest = std::move(dest);
    actions_.push_back(std::move(action));
    return *actions_.back();
}

Parser& Parser::add_parser(Action& subparsers, const U32& name, std::optional<U32> help) {
    if (help) {
        auto pseudo = std::make_unique<Action>();
        pseudo->dest = name;
        pseudo->metavar = name;
        pseudo->help = std::move(help);
        subparsers.choice_actions.push_back(std::move(pseudo));
    }
    // The prefix is the usage of this parser's positionals: the prog alone here.
    subparsers.parsers.emplace_back(name, std::make_unique<Parser>(prog_ + U" " + name));
    subparsers.choices->push_back(name);
    return *subparsers.parsers.back().second;
}

const Action* Parser::find_option(const U32& option_string) const {
    for (const auto& [s, a] : option_string_actions_)
        if (s == option_string) return a;
    return nullptr;
}

std::vector<const Action*> Parser::positionals() const {
    std::vector<const Action*> out;
    for (const auto& a : actions_)
        if (a->option_strings.empty()) out.push_back(a.get());
    return out;
}

U32 Parser::format_usage() const {
    std::vector<const Action*> actions;
    for (const auto& a : actions_) actions.push_back(a.get());
    Formatter f(prog_, terminal_columns());
    return Formatter::finish(f.format_usage(actions));
}

U32 Parser::format_help() const {
    std::vector<const Action*> actions, positional, optional;
    for (const auto& a : actions_) {
        actions.push_back(a.get());
        (a->option_strings.empty() ? positional : optional).push_back(a.get());
    }
    Formatter f(prog_, terminal_columns());
    f.indent();
    for (const Action* a : positional) f.add_argument(*a);
    for (const Action* a : optional) f.add_argument(*a);
    f.dedent();
    U32 help = f.format_usage(actions);
    if (description_) help += f.format_text(*description_);
    help += f.format_section(U"positional arguments", positional);
    help += f.format_section(U"options", optional);
    return Formatter::finish(help);
}

void Parser::error(const U32& message, std::ostream& err) const {
    // sys.stderr writes with errors="backslashreplace".
    err << pyio::backslashreplace(format_usage());
    err << pyio::backslashreplace(prog_ + U": error: " + message + U"\n");
    throw Exit{2};
}

std::optional<std::vector<Parser::OptionTuple>> Parser::parse_optional(const U32& arg) const {
    if (arg.empty() || arg[0] != U'-') return std::nullopt;
    if (const Action* a = find_option(arg)) return std::vector<OptionTuple>{{a, arg, std::nullopt, std::nullopt}};
    if (arg.size() == 1) return std::nullopt;
    const size_t eq = arg.find(U'=');
    if (eq != U32::npos) {
        if (const Action* a = find_option(arg.substr(0, eq)))
            return std::vector<OptionTuple>{{a, arg.substr(0, eq), U32(U"="), arg.substr(eq + 1)}};
    }
    std::vector<OptionTuple> tuples = get_option_tuples(arg);
    if (!tuples.empty()) return tuples;
    if (negative_number(arg)) return std::nullopt;
    if (arg.find(U' ') != U32::npos) return std::nullopt;
    return std::vector<OptionTuple>{{nullptr, arg, std::nullopt, std::nullopt}};
}

std::vector<Parser::OptionTuple> Parser::get_option_tuples(const U32& option_string) const {
    std::vector<OptionTuple> result;
    const size_t eq = option_string.find(U'=');
    const U32 prefix = option_string.substr(0, eq);
    std::optional<U32> sep, explicit_arg;
    if (eq != U32::npos) {
        sep = U"=";
        explicit_arg = option_string.substr(eq + 1);
    }
    if (option_string[1] == U'-') {
        for (const auto& [s, a] : option_string_actions_)
            if (starts_with(s, prefix)) result.push_back({a, s, sep, explicit_arg});
    } else {
        const U32 short_prefix = option_string.substr(0, 2);
        const U32 short_explicit = option_string.substr(2);
        for (const auto& [s, a] : option_string_actions_) {
            if (s == short_prefix) result.push_back({a, s, U32(), short_explicit});
            else if (starts_with(s, prefix)) result.push_back({a, s, sep, explicit_arg});
        }
    }
    return result;
}

void Parser::take_action(const Action& action, std::vector<U32> args, const std::optional<U32>& /*option_string*/,
                         Namespace& ns, std::vector<U32>& unrecognized, std::ostream& out,
                         std::ostream& err) const {
    // _get_values and _check_value.
    auto check = [&](const U32& value) {
        if (action.choices && std::find(action.choices->begin(), action.choices->end(), value) == action.choices->end())
            throw ArgumentError{action_name(action), U"invalid choice: " + repr(value) + U" (choose from " +
                                                         join(*action.choices, U", ") + U")"};
    };
    switch (action.kind) {
        case Action::Kind::Help:
            out << to_utf8(format_help());
            throw Exit{0};
        case Action::Kind::StoreTrue:
            ns[action.dest] = U"True";
            return;
        case Action::Kind::Store: {
            U32 value = args.at(0);
            if (action.int_type) {
                const auto n = py_int(to_utf8(value));
                if (!n) throw ArgumentError{action_name(action), U"invalid int value: " + repr(value)};
                value = u(n->str());
            }
            check(value);
            ns[action.dest] = value;
            return;
        }
        case Action::Kind::SubParsers: {
            check(args.at(0));
            ns[action.dest] = args[0];
            const Parser* parser = nullptr;
            for (const auto& [name, p] : action.parsers)
                if (name == args[0]) parser = p.get();
            auto [sub_ns, rest] = parser->parse_known_args({args.begin() + 1, args.end()}, out, err);
            for (auto& [k, v] : sub_ns) ns[k] = v;
            unrecognized.insert(unrecognized.end(), rest.begin(), rest.end());
            return;
        }
    }
}

std::pair<Namespace, std::vector<U32>> Parser::parse_known_args(const std::vector<U32>& args, std::ostream& out,
                                                                std::ostream& err) const {
    Namespace ns;
    for (const auto& a : actions_)
        if (a->kind != Action::Kind::Help && !ns.count(a->dest)) ns[a->dest] = a->default_value;
    std::vector<U32> extras;
    try {
        extras = parse_known_args_impl(args, ns, out, err);
    } catch (const ArgumentError& e) {
        error(e.str(), err);
    }
    return {ns, extras};
}

std::vector<U32> Parser::parse_known_args_impl(const std::vector<U32>& arg_strings, Namespace& ns,
                                               std::ostream& out, std::ostream& err) const {
    std::map<size_t, std::vector<OptionTuple>> option_string_indices;
    U32 pattern;
    for (size_t i = 0; i < arg_strings.size(); ++i) {
        if (arg_strings[i] == U"--") {
            pattern.push_back(U'-');
            for (++i; i < arg_strings.size(); ++i) pattern.push_back(U'A');
            break;
        }
        if (auto tuples = parse_optional(arg_strings[i])) {
            option_string_indices[i] = std::move(*tuples);
            pattern.push_back(U'O');
        } else {
            pattern.push_back(U'A');
        }
    }

    std::set<const Action*> seen;
    std::vector<U32> extras, unrecognized;
    auto act = [&](const Action& action, std::vector<U32> args, const std::optional<U32>& option_string) {
        seen.insert(&action);
        take_action(action, std::move(args), option_string, ns, unrecognized, out, err);
    };
    auto match_argument = [&](const Action& action, const U32& selected) -> size_t {
        const auto m = match_groups({&action}, selected);
        if (!m) throw ArgumentError{action_name(action), U"expected one argument"};
        return m->lengths[0];
    };

    auto consume_optional = [&](size_t start_index) -> size_t {
        const std::vector<OptionTuple>& tuples = option_string_indices.at(start_index);
        if (tuples.size() > 1) {
            std::vector<U32> options;
            for (const auto& t : tuples) options.push_back(t.option_string);
            throw ArgumentError{std::nullopt, U"ambiguous option: " + arg_strings[start_index] + U" could match " +
                                                  join(options, U", ")};
        }
        const Action* action = tuples[0].action;
        U32 option_string = tuples[0].option_string;
        std::optional<U32> sep = tuples[0].sep, explicit_arg = tuples[0].explicit_arg;
        std::vector<std::tuple<const Action*, std::vector<U32>, U32>> action_tuples;
        size_t stop = start_index + 1;
        while (true) {
            if (action == nullptr) {
                extras.push_back(arg_strings[start_index]);
                return start_index + 1;
            }
            if (explicit_arg) {
                const size_t arg_count = match_argument(*action, U"A");
                if (arg_count == 0 && option_string[1] != U'-' && !explicit_arg->empty()) {
                    if ((sep && !sep->empty()) || (*explicit_arg)[0] == U'-')
                        throw ArgumentError{action_name(*action), U"ignored explicit argument " + repr(*explicit_arg)};
                    action_tuples.emplace_back(action, std::vector<U32>{}, option_string);
                    const U32 joined = U32(1, option_string[0]) + explicit_arg->substr(0, 1);
                    if (const Action* next = find_option(joined)) {
                        option_string = joined;
                        action = next;
                        *explicit_arg = explicit_arg->substr(1);
                        if (explicit_arg->empty()) {
                            sep.reset();
                            explicit_arg.reset();
                        } else if ((*explicit_arg)[0] == U'=') {
                            sep = U"=";
                            *explicit_arg = explicit_arg->substr(1);
                        } else {
                            sep = U"";
                        }
                    } else {
                        extras.push_back(U32(1, option_string[0]) + *explicit_arg);
                        stop = start_index + 1;
                        break;
                    }
                } else if (arg_count == 1) {
                    stop = start_index + 1;
                    action_tuples.emplace_back(action, std::vector<U32>{*explicit_arg}, option_string);
                    break;
                } else {
                    throw ArgumentError{action_name(*action), U"ignored explicit argument " + repr(*explicit_arg)};
                }
            } else {
                const size_t start = start_index + 1;
                const size_t arg_count = match_argument(*action, pattern.substr(std::min(start, pattern.size())));
                stop = start + arg_count;
                action_tuples.emplace_back(
                    action, std::vector<U32>(arg_strings.begin() + static_cast<long>(start),
                                             arg_strings.begin() + static_cast<long>(stop)),
                    option_string);
                break;
            }
        }
        for (auto& [a, args, os] : action_tuples) act(*a, args, os);
        return stop;
    };

    std::vector<const Action*> remaining = positionals();
    auto consume_positionals = [&](size_t start_index) -> size_t {
        const U32 selected = pattern.substr(std::min(start_index, pattern.size()));
        std::vector<size_t> arg_counts;
        for (size_t i = remaining.size(); i > 0; --i) {
            const std::vector<const Action*> slice(remaining.begin(), remaining.begin() + static_cast<long>(i));
            if (const auto m = match_groups(slice, selected)) {
                arg_counts = m->lengths;
                if (m->end < selected.size() && selected[m->end] == U'O')
                    while (!arg_counts.empty() && arg_counts.back() == 0) arg_counts.pop_back();
                break;
            }
        }
        for (size_t i = 0; i < arg_counts.size(); ++i) {
            const Action& action = *remaining[i];
            const size_t count = arg_counts[i];
            std::vector<U32> args(arg_strings.begin() + static_cast<long>(start_index),
                                  arg_strings.begin() + static_cast<long>(start_index + count));
            const auto remove_dashes = [&args] {
                const auto it = std::find(args.begin(), args.end(), U32(U"--"));
                if (it != args.end()) args.erase(it);
            };
            if (action.nargs == Action::Nargs::Parser) {
                if (pattern[start_index] == U'-') remove_dashes();
            } else if (pattern.substr(start_index, count).find(U'-') != U32::npos) {
                remove_dashes();
            }
            start_index += count;
            act(action, args, std::nullopt);
        }
        remaining.erase(remaining.begin(), remaining.begin() + static_cast<long>(arg_counts.size()));
        return start_index;
    };

    size_t start_index = 0;
    const long max_option_index = option_string_indices.empty() ? -1 : static_cast<long>(option_string_indices.rbegin()->first);
    while (static_cast<long>(start_index) <= max_option_index) {
        const size_t next_option = option_string_indices.lower_bound(start_index)->first;
        if (start_index != next_option) {
            const size_t positionals_end = consume_positionals(start_index);
            if (positionals_end > start_index) {
                start_index = positionals_end;
                continue;
            }
            start_index = positionals_end;
        }
        if (!option_string_indices.count(start_index)) {
            extras.insert(extras.end(), arg_strings.begin() + static_cast<long>(start_index),
                          arg_strings.begin() + static_cast<long>(next_option));
            start_index = next_option;
        }
        start_index = consume_optional(start_index);
    }
    const size_t stop_index = consume_positionals(start_index);
    extras.insert(extras.end(), arg_strings.begin() + static_cast<long>(std::min(stop_index, arg_strings.size())),
                  arg_strings.end());

    std::vector<U32> required;
    for (const auto& a : actions_)
        if (!seen.count(a.get()) && a->required) required.push_back(*action_name(*a));
    if (!required.empty())
        throw ArgumentError{std::nullopt, U"the following arguments are required: " + join(required, U", ")};
    extras.insert(extras.end(), unrecognized.begin(), unrecognized.end());
    return extras;
}

Namespace Parser::parse_args(const std::vector<U32>& args, std::ostream& out, std::ostream& err) const {
    auto [ns, extras] = parse_known_args(args, out, err);
    if (!extras.empty()) error(U"unrecognized arguments: " + join(extras, U" "), err);
    return ns;
}

}  // namespace tugaphone::detail::argparse
