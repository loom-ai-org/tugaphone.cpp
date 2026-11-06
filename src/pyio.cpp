#include "pyio.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
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

std::string path_join(const std::string& a, const std::string& b) {
    if (!b.empty() && b.front() == '/') return b;
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
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

int print_exception(std::ostream& err) {
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

Stdout::Stdout(int fd) : fd_(fd), line_buffering_(isatty(fd) == 1), buffer_size_(8192) {
    struct stat st {};
    if (fstat(fd, &st) == 0 && st.st_blksize > 1) buffer_size_ = static_cast<size_t>(st.st_blksize);
}

std::streamsize Stdout::xsputn(const char* s, std::streamsize n) {
    constexpr size_t chunk_size = 8192;
    const std::string_view text(s, static_cast<size_t>(n));
    if (pending_.size() + text.size() > chunk_size) write_flush();
    pending_ += text;
    const bool needflush = line_buffering_ && text.find_first_of("\n\r") != std::string_view::npos;
    if (pending_.size() >= chunk_size || needflush) write_flush();
    if (needflush) buffer_flush();
    return n;
}

Stdout::int_type Stdout::overflow(int_type c) {
    if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
    const char ch = traits_type::to_char_type(c);
    xsputn(&ch, 1);
    return c;
}

void Stdout::flush() {
    write_flush();
    buffer_flush();
}

// The pending bytes leave the TextIOWrapper before the buffer takes them, so
// a failed write drops them.
void Stdout::write_flush() {
    std::string bytes;
    bytes.swap(pending_);
    if (!bytes.empty()) buffer_write(bytes);
}

void Stdout::buffer_write(const std::string& bytes) {
    if (bytes.size() <= buffer_size_ - buffer_.size()) {
        buffer_ += bytes;
        return;
    }
    buffer_flush();
    size_t written = 0;
    while (bytes.size() - written > buffer_size_) written += raw_write(bytes.data() + written, bytes.size() - written);
    buffer_.assign(bytes, written);
}

// A failed write keeps the bytes that did not reach the descriptor.
void Stdout::buffer_flush() {
    size_t written = 0;
    try {
        while (written < buffer_.size()) written += raw_write(buffer_.data() + written, buffer_.size() - written);
    } catch (...) {
        buffer_.erase(0, written);
        throw;
    }
    buffer_.clear();
}

size_t Stdout::raw_write(const char* data, size_t size) {
    while (true) {
        const ssize_t n = ::write(fd_, data, size);
        if (n >= 0) return static_cast<size_t>(n);
        if (errno != EINTR) raise_os_error(errno);
    }
}

int run_main(const std::function<int()>& body) {
    Stdout stdout_buf(STDOUT_FILENO);
    std::streambuf* const previous = std::cout.rdbuf(&stdout_buf);
    std::cout.exceptions(std::ios::badbit);
    // Python's stderr does not flush stdout, and a flush of a failed stdout
    // would throw.
    std::ostream* const tie = std::cerr.tie(nullptr);
    int status = body();
    try {
        stdout_buf.flush();
    } catch (const Error& e) {
        std::cerr << "Exception ignored in: <_io.TextIOWrapper name='<stdout>' mode='w' encoding='utf-8'>\n"
                  << e.python_type() << ": " << e.what() << "\n";
        std::cerr.flush();
        status = 120;
    }
    std::cerr.tie(tie);
    std::cout.exceptions(std::ios::goodbit);
    std::cout.rdbuf(previous);
    return status;
}

}  // namespace tugaphone::detail::pyio
