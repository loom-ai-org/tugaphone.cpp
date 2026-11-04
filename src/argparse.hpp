// The part of Python 3.12's argparse that the tugaphone command lines use:
// store, store_true and help actions, int values, choices, one level of
// subcommands, prefix matching of long options, "--", and the help and usage
// formatter with textwrap. Messages, usage lines and help text are byte for
// byte those of argparse, for the same terminal width.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "detail.hpp"

namespace tugaphone::detail::argparse {

// textwrap.wrap(text, width) and textwrap.fill(text, width, initial_indent,
// subsequent_indent) with the default TextWrapper options.
std::vector<U32> wrap(U32View text, long width, U32View initial_indent = U"", U32View subsequent_indent = U"");
U32 fill(U32View text, long width, U32View initial_indent = U"", U32View subsequent_indent = U"");

// shutil.get_terminal_size().columns: COLUMNS when it is a positive integer,
// else the width of the terminal on standard output, else 80.
long terminal_columns();

// Python's parser.exit(status): the command line stops with this status.
struct Exit {
    int status;
};

class Parser;

struct Action {
    enum class Kind { Store, StoreTrue, Help, SubParsers };
    enum class Nargs { One, Zero, Parser };  // nargs=None, 0 and argparse.PARSER

    Kind kind = Kind::Store;
    std::vector<U32> option_strings;
    U32 dest;
    Nargs nargs = Nargs::One;
    bool required = false;
    std::optional<std::vector<U32>> choices;
    std::optional<U32> help;
    std::optional<U32> metavar;
    std::optional<U32> default_value;  // the value when absent; nullopt is None
    bool int_type = false;             // type=int

    // Subcommands: the parsers in insertion order and their help entries.
    std::vector<std::pair<U32, std::unique_ptr<Parser>>> parsers;
    std::vector<std::unique_ptr<Action>> choice_actions;
};

// The parsed values by dest. A store_true value is "True"; an absent value
// without a default is nullopt.
using Namespace = std::map<U32, std::optional<U32>>;

class Parser {
public:
    explicit Parser(U32 prog, std::optional<U32> description = std::nullopt, bool add_help = true);

    // add_argument with option strings (an optional) or a name (a positional).
    Action& add_argument(std::vector<U32> names, Action spec);
    // add_subparsers(dest=..., required=...).
    Action& add_subparsers(U32 dest, bool required);
    // subparsers.add_parser(name, help=...).
    Parser& add_parser(Action& subparsers, const U32& name, std::optional<U32> help);

    // parse_args: the namespace, or Exit after writing help to `out` or an
    // error to `err`.
    Namespace parse_args(const std::vector<U32>& args, std::ostream& out, std::ostream& err) const;

    U32 format_usage() const;
    U32 format_help() const;
    const U32& prog() const { return prog_; }

private:
    struct ArgumentError {
        std::optional<U32> argument_name;
        U32 message;
        U32 str() const;
    };
    struct OptionTuple {
        const Action* action;  // nullptr: no such option in this parser
        U32 option_string;
        std::optional<U32> sep, explicit_arg;
    };
    struct State;

    std::pair<Namespace, std::vector<U32>> parse_known_args(const std::vector<U32>& args, std::ostream& out,
                                                           std::ostream& err) const;
    std::vector<U32> parse_known_args_impl(const std::vector<U32>& args, Namespace& ns, std::ostream& out,
                                           std::ostream& err) const;
    std::optional<std::vector<OptionTuple>> parse_optional(const U32& arg) const;
    std::vector<OptionTuple> get_option_tuples(const U32& option_string) const;
    void take_action(const Action& action, std::vector<U32> args, const std::optional<U32>& option_string,
                     Namespace& ns, std::vector<U32>& unrecognized, std::ostream& out, std::ostream& err) const;
    [[noreturn]] void error(const U32& message, std::ostream& err) const;
    const Action* find_option(const U32& option_string) const;
    std::vector<const Action*> positionals() const;

    U32 prog_;
    std::optional<U32> description_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<std::pair<U32, const Action*>> option_string_actions_;  // insertion order
};

// _get_action_name(action).
std::optional<U32> action_name(const Action& action);

}  // namespace tugaphone::detail::argparse
