#include "pyio.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "detail.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::detail::pyio {

namespace {

[[noreturn]] void raise_os_error(int code, const std::string& path) {
    const std::string message =
        "[Errno " + std::to_string(code) + "] " + std::strerror(code) + ": " + py_str_repr(from_utf8(path));
    switch (code) {
        case ENOENT: throw Error("FileNotFoundError", message);
        case EISDIR: throw Error("IsADirectoryError", message);
        case ENOTDIR: throw Error("NotADirectoryError", message);
        case EACCES:
        case EPERM: throw Error("PermissionError", message);
        default: throw Error("OSError", message);
    }
}

// The length of the well-formed UTF-8 sequence at `i`, or 0.
size_t utf8_length(const std::string& s, size_t i) {
    const auto b = [&](size_t k) { return k < s.size() ? static_cast<unsigned char>(s[k]) : 0x100u; };
    const unsigned c = b(i);
    auto cont = [&](size_t k, unsigned lo = 0x80, unsigned hi = 0xBF) { return b(k) >= lo && b(k) <= hi; };
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return cont(i + 1) ? 2 : 0;
    if (c == 0xE0) return cont(i + 1, 0xA0) && cont(i + 2) ? 3 : 0;
    if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) return cont(i + 1) && cont(i + 2) ? 3 : 0;
    if (c == 0xED) return cont(i + 1, 0x80, 0x9F) && cont(i + 2) ? 3 : 0;
    if (c == 0xF0) return cont(i + 1, 0x90) && cont(i + 2) && cont(i + 3) ? 4 : 0;
    if (c >= 0xF1 && c <= 0xF3) return cont(i + 1) && cont(i + 2) && cont(i + 3) ? 4 : 0;
    if (c == 0xF4) return cont(i + 1, 0x80, 0x8F) && cont(i + 2) && cont(i + 3) ? 4 : 0;
    return 0;
}

}  // namespace

std::string read_text(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) raise_os_error(errno, path);
    struct stat st {};
    if (::fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        ::close(fd);
        raise_os_error(EISDIR, path);
    }
    std::string bytes;
    char buffer[65536];
    while (true) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n < 0) {
            const int code = errno;
            ::close(fd);
            raise_os_error(code, path);
        }
        if (n == 0) break;
        bytes.append(buffer, static_cast<size_t>(n));
    }
    ::close(fd);
    for (size_t i = 0; i < bytes.size();) {
        const size_t n = utf8_length(bytes, i);
        if (n == 0) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "0x%02x", static_cast<unsigned char>(bytes[i]));
            throw Error("UnicodeDecodeError", std::string("'utf-8' codec can't decode byte ") + hex + " in position " +
                                                  std::to_string(i));
        }
        i += n;
    }
    std::string text;
    text.reserve(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r') {
            text.push_back('\n');
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i;
        } else {
            text.push_back(bytes[i]);
        }
    }
    return text;
}

void write_text(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) raise_os_error(errno, path);
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const int code = errno;
    std::fclose(f);
    if (!ok) raise_os_error(code, path);
}

std::string qualified_name(const std::string& python_type) {
    if (python_type == "JSONDecodeError") return "json.decoder.JSONDecodeError";
    return python_type;
}

}  // namespace tugaphone::detail::pyio
