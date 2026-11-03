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
    return TUGAPHONE_DEFAULT_DATA_DIR;
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
