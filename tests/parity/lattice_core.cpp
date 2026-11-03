#include "tugaphone/accent.hpp"
#include "tugaphone/lattice_core.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

#include <zlib.h>

#include "common.hpp"
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
    return J(lc::is_stale(path));
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
