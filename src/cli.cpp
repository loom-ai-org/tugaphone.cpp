#include "cli.hpp"

#include <memory>
#include <optional>

#include "argparse.hpp"
#include "detail.hpp"
#include "pyio.hpp"
#include "tugaphone/accent.hpp"
#include "tugaphone/lattice_core.hpp"
#include "tugaphone/registry.hpp"

namespace tugaphone::cli {

namespace {

namespace ap = detail::argparse;
using detail::U32;
using detail::u;

// The docstring of tugaphone/__main__.py, which argparse reflows as the
// description.
constexpr const char* kDescription =
    "Command-line interface for tugaphone.\n\n"
    "    tugaphone phonemize \"o gato dorme\" --lect pt-BR\n"
    "    tugaphone force-accent \"o vinho verde\" --lect pt-PT-x-porto --mode respell\n"
    "    tugaphone force-accent \"a tia\" --lect pt-BR --mode respell --base pt-PT         --overlay tweaks.json\n"
    "    tugaphone list\n\n"
    "``force-accent`` is the accent-forcing entry point (see\n"
    ":mod:`tugaphone.accent`): ``--mode ipa`` prints the target lect's IPA for a\n"
    "phoneme-input TTS, ``--mode respell`` prints Portuguese text respelled so a\n"
    "grapheme-input TTS speaking ``--base`` pronounces the target accent.\n";

ap::Action store(std::optional<U32> help, std::optional<U32> default_value = std::nullopt, bool required = false) {
    ap::Action a;
    a.help = std::move(help);
    a.default_value = std::move(default_value);
    a.required = required;
    return a;
}

std::unique_ptr<ap::Parser> build_parser() {
    auto parser = std::make_unique<ap::Parser>(U"tugaphone", u(kDescription));
    ap::Action& sub = parser->add_subparsers(U"command", true);

    ap::Parser& ph = parser->add_parser(sub, U"phonemize", U32(U"transcribe text to IPA"));
    ph.add_argument({U"text"}, store(std::nullopt));
    ph.add_argument({U"--lect", U"--lang"}, store(U32(U"target lect code (default: pt-PT)"), U32(U"pt-PT")));

    ap::Parser& fa = parser->add_parser(sub, U"force-accent", U32(U"force text/IPA into a target accent for a TTS"));
    fa.add_argument({U"text"}, store(std::nullopt));
    fa.add_argument({U"--lect", U"--lang"}, store(U32(U"target accent lect code"), std::nullopt, true));
    ap::Action mode = store(U32(U"'ipa' for phoneme-input TTS, 'respell' for grapheme-input TTS (default: ipa)"),
                            U32(U"ipa"));
    mode.choices = std::vector<U32>{U"ipa", U"respell"};
    fa.add_argument({U"--mode"}, std::move(mode));
    fa.add_argument({U"--base"},
                    store(U32(U"base accent the grapheme voice speaks (respell mode; default: pt-PT)"), U32(U"pt-PT")));
    fa.add_argument({U"--overlay"}, store(U32(U"path to a JSON AccentOverlay of user tweaks")));

    parser->add_parser(sub, U"list", U32(U"list the supported dialect/lect codes"));
    return parser;
}

// The bytes of the argument, as os.fsencode gives them back.
std::string value(const ap::Namespace& ns, const U32& dest) {
    const auto it = ns.find(dest);
    return it != ns.end() && it->second ? detail::pyio::fsencode(*it->second) : std::string();
}

// _load_overlay(path): None for no path.
std::optional<accent::AccentOverlay> load_overlay(const std::string& path) {
    if (path.empty()) return std::nullopt;
    return accent::AccentOverlay::from_json(detail::pyio::read_text(path));
}

}  // namespace

int main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const auto parser = build_parser();
    std::vector<U32> argv;
    for (const std::string& a : args) argv.push_back(detail::pyio::fsdecode(a));
    try {
        const ap::Namespace ns = parser->parse_args(argv, out, err);
        const std::string command = value(ns, U"command");
        if (command == "phonemize") {
            out << lattice_core::phonemize(value(ns, U"text"), value(ns, U"lect")) << "\n";
        } else if (command == "force-accent") {
            const std::string text = value(ns, U"text"), lect = value(ns, U"lect");
            const std::string mode = value(ns, U"mode"), base = value(ns, U"base");
            const auto overlay = load_overlay(value(ns, U"overlay"));
            out << accent::force_accent(text, lect, mode, base, overlay ? &*overlay : nullptr) << "\n";
        } else if (command == "list") {
            for (const std::string& code : registry::list_dialects()) out << code << "\n";
        }
        return 0;
    } catch (const ap::Exit& e) {
        return e.status;
    } catch (...) {
        return detail::pyio::print_exception(err);
    }
}

}  // namespace tugaphone::cli
