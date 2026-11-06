// Python's file and process plumbing as the command-line tools use it:
// open(path, encoding="utf-8") in text mode, the OSError subclasses and their
// messages, os.path and pathlib path handling, argv decoding, and the line an
// uncaught exception prints.
#pragma once

#include <functional>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>

#include "detail.hpp"
#include "tugaphone/errors.hpp"

namespace tugaphone::detail::pyio {

// The OSError subclass Python raises for an errno: FileNotFoundError,
// PermissionError and so on, or OSError itself.
std::string os_error_class(int code);

// OSError(errno, strerror[, filename[, filename2]]): the subclass of the
// errno, with the message "[Errno N] <strerror>", then ": '<filename>'", then
// " -> '<filename2>'".
[[noreturn]] void raise_os_error(int code);
[[noreturn]] void raise_os_error(int code, const std::string& path);
[[noreturn]] void raise_os_error(int code, const std::string& path, const std::string& path2);

// bytes.decode("utf-8"): UnicodeDecodeError with CPython's message for the
// first ill-formed sequence, its byte range and its reason.
void check_utf8(const std::string& bytes);

// open(path, encoding="utf-8").read(). FileNotFoundError, IsADirectoryError,
// PermissionError or OSError when the file cannot be read, with Python's
// "[Errno N] <strerror>: '<path>'" message; UnicodeDecodeError for bytes that
// are not UTF-8. "\r\n" and "\r" read as "\n".
std::string read_text(const std::string& path);

// open(path, "w", encoding="utf-8").write(text): the bytes as given, since
// text mode on POSIX writes "\n" unchanged.
void write_text(const std::string& path, const std::string& text);

// os.path.join(a, b): b alone when it is absolute.
std::string path_join(const std::string& a, const std::string& b);

// os.makedirs(path, exist_ok=True).
void makedirs(const std::string& path);

// The arguments as Python's sys.argv holds them: UTF-8, with each byte that
// is not part of a well-formed sequence as a lone surrogate U+DC80..U+DCFF.
U32 fsdecode(std::string_view bytes);
// os.fsencode: the inverse of fsdecode.
std::string fsencode(U32View text);
// Text as sys.stderr writes it: UTF-8, and a lone surrogate as "\udcNN".
std::string backslashreplace(U32View text);

// The name Python prints in a traceback's last line: the module for a class
// outside builtins ("json.decoder.JSONDecodeError"), the bare name otherwise.
std::string qualified_name(const std::string& python_type);

// The end of an uncaught exception, called from inside a catch block:
// writes "<class>: <message>" for the exception in flight to `err` as the
// last line of a Python traceback reads, and returns 1. It knows the
// exceptions of tugaphone and of the libraries it calls.
int print_exception(std::ostream& err);

// sys.stdout of CPython on a file descriptor: a TextIOWrapper that keeps up
// to 8192 bytes pending, over a BufferedWriter with the st_blksize of the
// descriptor as its buffer, and a flush at each line on a terminal. Each
// xsputn() is one write() call of Python. A write that reaches the
// descriptor and fails throws the OSError of its errno, as print() raises
// it. The error leaves operator<< only when the stream has badbit in
// exceptions().
class Stdout : public std::streambuf {
public:
    explicit Stdout(int fd);
    // TextIOWrapper.flush().
    void flush();

protected:
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int_type overflow(int_type c) override;

private:
    void write_flush();
    void buffer_write(const std::string& bytes);
    void buffer_flush();
    size_t raw_write(const char* data, size_t size);

    int fd_;
    bool line_buffering_;
    size_t buffer_size_;
    std::string pending_;
    std::string buffer_;
};

// The interpreter around __main__: runs `body` with std::cout writing to
// descriptor 1 through Stdout, then flushes it. When that flush fails,
// Python writes "Exception ignored in: <stdout>" and the OSError to stderr
// and exits with 120; this returns 120 then, and the status of `body`
// otherwise.
int run_main(const std::function<int()>& body);

}  // namespace tugaphone::detail::pyio
