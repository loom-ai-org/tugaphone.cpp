#include "tugaphone/accent.hpp"
#include "tugaphone/lattice_core.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

#include <orthography2ipa/orthography2ipa.hpp>
#include <zlib.h>

#include "common.hpp"
#include "detail.hpp"
#include "modules.hpp"

namespace parity {

namespace lc = tugaphone::lattice_core;
namespace fs = std::filesystem;

namespace {

std::string read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_bytes(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

unsigned long crc_of(const std::string& data) {
    return crc32(0L, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
}

// A scratch directory for the lexicon records, under the lexicon cache.
std::string scratch_dir() {
    const std::string dir = lc::lexicon_cache_dir() + "/parity";
    fs::create_directories(dir);
    return dir;
}

std::string fake_contact(const std::string& token, const std::string& lang) {
    return token.rfind('x', 0) == 0 ? "" : "<" + lang + ":" + token + ">";
}

std::string is_stale(const std::string& scenario) {
    const std::string dir = scratch_dir() + "/stale";
    fs::create_directories(dir);
    const std::string path = dir + "/" + scenario + ".tsv";
    const std::string stamp = lc::stamp_path(path);
    fs::remove(path);
    fs::remove(stamp);
    const std::string version = lc::tugalex_version();
    if (scenario == "empty" || scenario == "empty_stamped") write_bytes(path, "");
    if (scenario == "no_stamp" || scenario == "stamp" || scenario == "stamp_spaces" || scenario == "stamp_other")
        write_bytes(path, "a\tb\n");
    if (scenario == "stamp" || scenario == "empty_stamped") write_bytes(stamp, version);
    if (scenario == "stamp_spaces") write_bytes(stamp, " " + version + "\n");
    if (scenario == "stamp_other") write_bytes(stamp, "0");
    if (scenario == "stamp_is_dir") {
        write_bytes(path, "a\tb\n");
        fs::create_directories(stamp);
    }
    const std::string out = J(lc::is_stale(path));
    fs::remove(path);
    fs::remove(stamp);
    return out;
}

// _is_stale on a stamp of the given bytes, written as hex in the record.
std::string is_stale_bytes(const std::string& hex) {
    const std::string dir = scratch_dir() + "/stale_bytes";
    fs::create_directories(dir);
    const std::string path = dir + "/x.tsv";
    std::string stamp;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) stamp.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    write_bytes(path, "a\tb\n");
    write_bytes(lc::stamp_path(path), stamp);
    try {
        return J(lc::is_stale(path));
    } catch (const tugaphone::Error& e) {
        return "{\"err\":" + J(e.python_type()) + ",\"msg\":" + J(std::string(e.what())) + "}";
    }
}

// _write_lexicon under a broken cache: an unknown region, a parent that is a
// regular file, and a stamp path that is a directory.
std::string write_lexicon_error(const std::string& scenario) {
    const std::string dir = scratch_dir() + "/broken";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string region = "lbx", path;
    if (scenario == "xyz") {
        region = "xyz";
        path = dir + "/xyz.tsv";
    } else if (scenario == "under_file") {
        write_bytes(dir + "/file", "x");
        path = dir + "/file/sub/x.tsv";
    } else if (scenario == "stamp_is_dir") {
        fs::create_directories(dir + "/stamp_dir/x.version");
        write_bytes(dir + "/stamp_dir/x.tsv", "a\tb\n");
        path = dir + "/stamp_dir/x.tsv";
    }
    const std::string out = guard([&] {
        lc::write_lexicon(region, path);
        return std::string("null");
    });
    fs::remove_all(dir);
    return out;
}

// _LEXICON_CACHE under the environment of the record, the three variables it
// reads unset unless the record sets them.
std::string lexicon_cache_dir(const json_line::Value& env) {
    static const char* const names[] = {"HOME", "XDG_CACHE_HOME", "TUGAPHONE_LEXICON_DIR"};
    std::vector<std::optional<std::string>> saved;
    for (const char* name : names) {
        const char* value = std::getenv(name);
        saved.push_back(value ? std::optional<std::string>(value) : std::nullopt);
        unsetenv(name);
    }
    for (const auto& [name, value] : env.members) setenv(name.c_str(), value.str.c_str(), 1);
    const std::string out = J(tugaphone::detail::default_lexicon_cache_dir());
    for (size_t i = 0; i < saved.size(); ++i) {
        if (saved[i]) {
            setenv(names[i], saved[i]->c_str(), 1);
        } else {
            unsetenv(names[i]);
        }
    }
    return out;
}

// [[surface, ipa], ...] of engine(lect).sentence_lattice(text).
std::string engine_lattice(const std::string& text, const std::string& lect) {
    std::string out = "[";
    bool first = true;
    for (const auto& word : lc::engine(lect).sentence_lattice(text).words) {
        out += (first ? "[" : ",[") + J(word.surface) + "," + J(word.ipa) + "]";
        first = false;
    }
    return out + "]";
}

// [[word, [[grapheme, top1_ipa], ...]], ...] of engine(lect).features(text).
std::string engine_features(const std::string& text, const std::string& lect) {
    std::string out = "[";
    bool first_word = true;
    for (const auto& word : lc::engine(lect).features(text)) {
        out += (first_word ? "[" : ",[") + J(word.word) + ",[";
        bool first = true;
        for (const auto& g : word.graphemes) {
            out += (first ? "[" : ",[") + J(g.grapheme) + "," + J(g.top1_ipa) + "]";
            first = false;
        }
        out += "]]";
        first_word = false;
    }
    return out + "]";
}

}  // namespace

std::string record_lattice_core(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "phonemize") {
        return head(v) + guard([&] { return J(lc::phonemize(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "runs") {
        std::vector<lc::Run> runs;
        for (const auto& r : v[2].items) runs.emplace_back(opt_str(r[0]), r[1].str);
        return head(v) + guard([&] {
                   lc::validate_contact("en");
                   return J(lc::phonemize_runs(v[1].str, runs, fake_contact));
               }) + "]";
    }
    if (fn == "respell_word") {
        return head(v) + guard([&] { return J(tugaphone::accent::respell_word(v[1].str, v[2].str)); }) + "]";
    }
    if (fn == "force_accent") {
        return head(v) + guard([&] { return J(tugaphone::accent::force_accent(v[1].str, v[2].str, v[3].str)); }) + "]";
    }
    if (fn == "normalize") return head(v) + guard([&] { return J(lc::normalize(v[1].str, v[2].str)); }) + "]";
    if (fn == "relayout") return head(v) + J(lc::relayout(v[1].str)) + "]";
    if (fn == "stamp_path") return head(v) + guard([&] { return J(lc::stamp_path(v[1].str)); }) + "]";
    if (fn == "tugalex_version") return "[" + J(fn) + "," + J(lc::tugalex_version()) + "]";
    if (fn == "VALID_CONTACT") return "[" + J(fn) + "," + J(lc::VALID_CONTACT()) + "]";
    if (fn == "validate_contact") {
        return head(v) + guard([&] {
                   lc::validate_contact(v[1].str);
                   return std::string("null");
               }) + "]";
    }
    if (fn == "is_stale") return head(v) + is_stale(v[1].str) + "]";
    if (fn == "is_stale_bytes") return head(v) + is_stale_bytes(v[1].str) + "]";
    if (fn == "write_lexicon_error") return head(v) + write_lexicon_error(v[1].str) + "]";
    if (fn == "lexicon_cache_dir") return head(v) + lexicon_cache_dir(v[1]) + "]";
    if (fn == "engine_lattice") return head(v) + guard([&] { return engine_lattice(v[1].str, v[2].str); }) + "]";
    if (fn == "engine_features") return head(v) + guard([&] { return engine_features(v[1].str, v[2].str); }) + "]";
    if (fn == "write_lexicon") {
        const std::string region = v[1].str;
        const std::string path = scratch_dir() + "/parity-" + region + ".tsv";
        lc::write_lexicon(region, path);
        const std::string data = read_bytes(path);
        long lines = 0;
        for (char c : data) lines += c == '\n';
        const std::string out = "[" + J(fn) + "," + J(region) + "," + J(crc_of(data)) + "," +
                                J(static_cast<long>(data.size())) + "," + J(lines) + "," +
                                J(read_bytes(lc::stamp_path(path))) + "]";
        fs::remove(path);
        fs::remove(lc::stamp_path(path));
        return out;
    }
    return "unknown call " + fn;
}

}  // namespace parity
