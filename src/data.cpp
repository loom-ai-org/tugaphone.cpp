#include <dlfcn.h>
#if defined(__linux__)
#include <sys/auxv.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>

#include "detail.hpp"

#ifndef TUGAPHONE_DEFAULT_DATA_DIR
#define TUGAPHONE_DEFAULT_DATA_DIR "data"
#endif
#ifndef TUGAPHONE_DATA_FROM_LIBDIR
#define TUGAPHONE_DATA_FROM_LIBDIR "../share/tugaphone"
#endif
#ifndef TUGAPHONE_DATA_FROM_BINDIR
#define TUGAPHONE_DATA_FROM_BINDIR "../share/tugaphone"
#endif

namespace tugaphone {

namespace {
std::mutex& data_dir_mutex() {
    static std::mutex m;
    return m;
}
std::optional<std::string>& data_dir_override() {
    static std::optional<std::string> value;
    return value;
}

// The installed data directory beside the shared library or the executable
// that holds this code, so that an install moved to another prefix finds its
// data. Empty when that directory does not hold the data.
std::string relocated_data_dir() {
    namespace fs = std::filesystem;
    Dl_info self{};
    if (dladdr(reinterpret_cast<void*>(&data_dir), &self) == 0 || self.dli_fname == nullptr) return {};
    fs::path object = self.dli_fname;
    // A shared library finds the data from LIBDIR, the main program from BINDIR.
    const char* relative = TUGAPHONE_DATA_FROM_LIBDIR;
#if defined(__linux__)
    // glibc names the main program by argv[0], which need not be a path.
    Dl_info program{};
    if (dladdr(reinterpret_cast<void*>(getauxval(AT_PHDR)), &program) != 0 &&
        program.dli_fbase == self.dli_fbase) {
        object = "/proc/self/exe";
        relative = TUGAPHONE_DATA_FROM_BINDIR;
    }
#elif defined(__APPLE__)
    if (self.dli_fbase == static_cast<const void*>(_dyld_get_image_header(0))) relative = TUGAPHONE_DATA_FROM_BINDIR;
#else
    return {};
#endif
    std::error_code ec;
    const fs::path candidate = (fs::canonical(object, ec).parent_path() / relative).lexically_normal();
    if (ec || !fs::is_regular_file(candidate / "letter_expressions.json", ec)) return {};
    return candidate.string();
}
}  // namespace

void set_data_dir(std::string path) {
    std::lock_guard<std::mutex> lock(data_dir_mutex());
    data_dir_override() = std::move(path);
}

std::string data_dir() {
    {
        std::lock_guard<std::mutex> lock(data_dir_mutex());
        if (data_dir_override()) return *data_dir_override();
    }
    if (const char* env = std::getenv("TUGAPHONE_DATA_DIR"); env != nullptr && *env != '\0') return env;
    static const std::string relocated = relocated_data_dir();
    return relocated.empty() ? TUGAPHONE_DEFAULT_DATA_DIR : relocated;
}

}  // namespace tugaphone

namespace tugaphone::detail {

std::string data_path(const std::string& relative) {
    return (std::filesystem::path(data_dir()) / relative).string();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("FileNotFoundError", "No such file or directory: '" + path + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace tugaphone::detail
