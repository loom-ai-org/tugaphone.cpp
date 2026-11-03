#include "pyio.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bifonia/bifonia.hpp>
#include <orthography2ipa/exceptions.hpp>
#include <orthography2ipa/markup.hpp>
#include <tugalex/tugalex.hpp>

#include "unicode_norm.hpp"


namespace tugaphone::detail::pyio {

namespace {

std::string os_error_message(int code) { return "[Errno " + std::to_string(code) + "] " + std::strerror(code); }

std::string quoted(const std::string& path) { return py_str_repr(fsdecode(path)); }

// CPython's strict UTF-8 decoder at byte i: the end of the well-formed
// sequence there and nullptr, or the end of the byte range it reports and
// the reason.
std::pair<size_t, const char*> utf8_sequence(const std::string& s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned c = b(i);
    if (c < 0x80) return {i + 1, nullptr};
    size_t need = 0;
    unsigned lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        need = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        need = 3;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 4;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else {
        return {i + 1, "invalid start byte"};
    }
    for (size_t k = 1; k < need; ++k) {
        if (i + k >= s.size()) return {s.size(), "unexpected end of data"};
        const unsigned d = b(i + k);
        if (k == 1 ? (d < lo || d > hi) : (d < 0x80 || d > 0xBF)) return {i + k, "invalid continuation byte"};
    }
    return {i + need, nullptr};
}

// pathlib's is_dir(): false for the errors it ignores.
bool is_dir(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    const int code = errno;
    if (code == ENOENT || code == ENOTDIR || code == EBADF || code == ELOOP) return false;
    raise_os_error(code, path);
}

// PurePosixPath(path).parent for a path that pure_path() gives.
std::string pure_parent(const std::string& path) {
    const size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    const std::string head = path.substr(0, slash);
    if (head.empty() || head == "/") return path.substr(0, slash + 1);
    return head;
}

}  // namespace

std::string os_error_class(int code) {
    switch (code) {
        case EAGAIN:
        case EALREADY:
        case EINPROGRESS: return "BlockingIOError";
        case ECHILD: return "ChildProcessError";
        case EPIPE:
        case ESHUTDOWN: return "BrokenPipeError";
        case ECONNABORTED: return "ConnectionAbortedError";
        case ECONNREFUSED: return "ConnectionRefusedError";
        case ECONNRESET: return "ConnectionResetError";
        case EEXIST: return "FileExistsError";
        case ENOENT: return "FileNotFoundError";
        case EISDIR: return "IsADirectoryError";
        case ENOTDIR: return "NotADirectoryError";
        case EINTR: return "InterruptedError";
        case EACCES:
        case EPERM: return "PermissionError";
        case ESRCH: return "ProcessLookupError";
        case ETIMEDOUT: return "TimeoutError";
        default: return "OSError";
    }
}

void raise_os_error(int code) { throw Error(os_error_class(code), os_error_message(code)); }

void raise_os_error(int code, const std::string& path) {
    throw Error(os_error_class(code), os_error_message(code) + ": " + quoted(path));
}

void raise_os_error(int code, const std::string& path, const std::string& path2) {
    throw Error(os_error_class(code), os_error_message(code) + ": " + quoted(path) + " -> " + quoted(path2));
}

void check_utf8(const std::string& bytes) {
    for (size_t i = 0; i < bytes.size();) {
        const auto [end, reason] = utf8_sequence(bytes, i);
        if (reason == nullptr) {
            i = end;
            continue;
        }
        std::string message = "'utf-8' codec can't decode ";
        if (end == i + 1) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "0x%02x", static_cast<unsigned char>(bytes[i]));
            message += std::string("byte ") + hex + " in position " + std::to_string(i);
        } else {
            message += "bytes in position " + std::to_string(i) + "-" + std::to_string(end - 1);
        }
        throw Error("UnicodeDecodeError", message + ": " + reason);
    }
}

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
            raise_os_error(code);
        }
        if (n == 0) break;
        bytes.append(buffer, static_cast<size_t>(n));
    }
    ::close(fd);
    check_utf8(bytes);
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
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) raise_os_error(errno, path);
    for (size_t done = 0; done < text.size();) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            const int code = errno;
            ::close(fd);
            raise_os_error(code);
        }
        done += static_cast<size_t>(n);
    }
    if (::close(fd) != 0) raise_os_error(errno);
}

void replace(const std::string& src, const std::string& dst) {
    if (::rename(src.c_str(), dst.c_str()) != 0) raise_os_error(errno, src, dst);
}

std::string path_join(const std::string& a, const std::string& b) {
    if (!b.empty() && b.front() == '/') return b;
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string pure_path(const std::string& path) {
    std::string root;
    if (path.rfind("//", 0) == 0 && path.rfind("///", 0) != 0) {
        root = "//";
    } else if (!path.empty() && path.front() == '/') {
        root = "/";
    }
    std::string out;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (!part.empty() && part != ".") out += (out.empty() ? "" : "/") + part;
        start = end + 1;
    }
    if (root.empty() && out.empty()) return ".";
    return root + out;
}

bool path_exists(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) return true;
    const int code = errno;
    if (code == ENOENT || code == ENOTDIR || code == EBADF || code == ELOOP) return false;
    raise_os_error(code, path);
}

void mkdir_parents(const std::string& path) {
    if (::mkdir(path.c_str(), 0777) == 0) return;
    int code = errno;
    if (code == ENOENT) {
        const std::string parent = pure_parent(path);
        if (parent == path) raise_os_error(code, path);
        mkdir_parents(parent);
        // self.mkdir(parents=False, exist_ok=True)
        if (::mkdir(path.c_str(), 0777) == 0) return;
        code = errno;
        if (code == ENOENT) raise_os_error(code, path);
    }
    if (!is_dir(path)) raise_os_error(code, path);
}

void makedirs(const std::string& path) {
    // os.path.split, and once more when the path ends in a slash
    auto split = [](const std::string& p) {
        const size_t i = p.rfind('/') + 1;  // 0 when there is no slash
        std::string head = p.substr(0, i), tail = p.substr(i);
        if (!head.empty() && head.find_first_not_of('/') != std::string::npos) {
            head.erase(head.find_last_not_of('/') + 1);
        }
        return std::make_pair(head, tail);
    };
    auto [head, tail] = split(path);
    if (tail.empty()) std::tie(head, tail) = split(head);
    struct stat st {};
    if (!head.empty() && !tail.empty() && ::stat(head.c_str(), &st) != 0) {
        try {
            makedirs(head);
        } catch (const Error& e) {
            if (e.python_type() != "FileExistsError") throw;
        }
        if (tail == ".") return;
    }
    if (::mkdir(path.c_str(), 0777) == 0) return;
    const int code = errno;
    if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) raise_os_error(code, path);
}

U32 fsdecode(std::string_view bytes) {
    const std::string s(bytes);
    U32 out;
    for (size_t i = 0; i < s.size();) {
        const auto [end, reason] = utf8_sequence(s, i);
        if (reason == nullptr) {
            out += from_utf8(std::string_view(s).substr(i, end - i));
            i = end;
        } else {
            for (; i < end; ++i) out.push_back(0xDC00 + static_cast<unsigned char>(s[i]));
        }
    }
    return out;
}

std::string fsencode(U32View text) {
    std::string out;
    for (const char32_t c : text) {
        if (c >= 0xDC80 && c <= 0xDCFF) {
            out.push_back(static_cast<char>(c - 0xDC00));
        } else {
            out += to_utf8(U32View(&c, 1));
        }
    }
    return out;
}

std::string backslashreplace(U32View text) {
    std::string out;
    for (const char32_t c : text) {
        if (c >= 0xD800 && c <= 0xDFFF) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "\\u%04x", static_cast<unsigned>(c));
            out += hex;
        } else {
            out += to_utf8(U32View(&c, 1));
        }
    }
    return out;
}

std::string qualified_name(const std::string& python_type) {
    if (python_type == "JSONDecodeError") return "json.decoder.JSONDecodeError";
    if (python_type == "error") return "re.error";
    return python_type;
}

int print_exception(std::ostream& out, std::ostream& err) {
    out.flush();
    std::string type, message;
    try {
        throw;
    } catch (const Error& e) {
        type = qualified_name(e.python_type());
        message = e.what();
    } catch (const orthography2ipa::markup::MarkupError& e) {
        type = "orthography2ipa.markup.MarkupError";
        message = e.what();
    } catch (const orthography2ipa::UnmappedScriptError& e) {
        type = "orthography2ipa.exceptions.UnmappedScriptError";
        message = e.what();
    } catch (const std::filesystem::filesystem_error& e) {
        const int code = e.code().value();
        type = os_error_class(code);
        message = os_error_message(code);
        if (!e.path1().empty()) message += ": " + quoted(e.path1().string());
        if (!e.path2().empty()) message += " -> " + quoted(e.path2().string());
    } catch (const tugalex::FileNotFoundError& e) {
        type = "FileNotFoundError";
        message = e.what();
    } catch (const tugalex::DataError& e) {
        type = e.python_type();
        message = e.what();
    } catch (const bifonia::FileNotFoundError& e) {
        type = "FileNotFoundError";
        message = e.what();
    } catch (const bifonia::JSONDecodeError& e) {
        type = "json.decoder.JSONDecodeError";
        message = e.what();
    } catch (const bifonia::UnicodeDecodeError& e) {
        type = "UnicodeDecodeError";
        message = e.what();
    } catch (const bifonia::KeyError& e) {
        type = "KeyError";
        message = e.what();
    } catch (const bifonia::IndexError& e) {
        type = "IndexError";
        message = e.what();
    } catch (const std::invalid_argument& e) {
        type = "ValueError";
        message = e.what();
    } catch (const std::bad_alloc&) {
        type = "MemoryError";
    } catch (const std::exception& e) {
        type = "Exception";
        message = e.what();
    }
    err << (message.empty() ? type : type + ": " + message) << "\n";
    err.flush();
    return 1;
}

int finish_stdout(int status) {
    errno = 0;
    const bool flushed = std::fflush(stdout) == 0;
    const int code = !flushed ? errno : EIO;  // EIO: a write failed before, its errno is gone
    if (flushed && !std::ferror(stdout)) return status;
    std::fprintf(stderr, "Exception ignored in: <_io.TextIOWrapper name='<stdout>' mode='w' encoding='utf-8'>\n%s: %s\n",
                 os_error_class(code).c_str(), os_error_message(code).c_str());
    std::fflush(stderr);
    return 120;
}

}  // namespace tugaphone::detail::pyio
