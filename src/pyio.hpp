// Python's text files as the command-line tools use them: open(path,
// encoding="utf-8") in text mode, with its OSError subclasses, strict UTF-8
// decoding and universal newlines.
#pragma once

#include <string>

#include "tugaphone/errors.hpp"

namespace tugaphone::detail::pyio {

// open(path, encoding="utf-8").read(). FileNotFoundError, IsADirectoryError,
// PermissionError or OSError when the file cannot be read, with Python's
// "[Errno N] <strerror>: '<path>'" message; UnicodeDecodeError for bytes that
// are not UTF-8. "\r\n" and "\r" read as "\n".
std::string read_text(const std::string& path);

// open(path, "w", encoding="utf-8").write(text): the bytes as given, since
// text mode on POSIX writes "\n" unchanged.
void write_text(const std::string& path, const std::string& text);

// The name Python prints in a traceback's last line: the module for a class
// outside builtins ("json.decoder.JSONDecodeError"), the bare name otherwise.
std::string qualified_name(const std::string& python_type);

}  // namespace tugaphone::detail::pyio
