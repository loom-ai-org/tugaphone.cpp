// Python exceptions as C++ exceptions. Each one names the Python class that
// the Python package raises in the same case.
#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace tugaphone {

class Error : public std::runtime_error {
public:
    Error(std::string python_type, const std::string& message)
        : std::runtime_error(message), python_type_(std::move(python_type)) {}
    // "ValueError", "KeyError" and so on.
    const std::string& python_type() const { return python_type_; }

private:
    std::string python_type_;
};

struct ValueError : Error {
    explicit ValueError(const std::string& message) : Error("ValueError", message) {}
};
struct KeyError : Error {
    explicit KeyError(const std::string& message) : Error("KeyError", message) {}
};
struct IndexError : Error {
    explicit IndexError(const std::string& message) : Error("IndexError", message) {}
};
struct TypeError : Error {
    explicit TypeError(const std::string& message) : Error("TypeError", message) {}
};
struct NotImplementedError : Error {
    explicit NotImplementedError(const std::string& message) : Error("NotImplementedError", message) {}
};

// The directory that holds the data files. The first of these that is set
// wins: set_data_dir(), the TUGAPHONE_DATA_DIR environment variable, the data
// directory of the install that holds the library (found from the path of the
// shared library or of the program), the install location compiled into the
// library.
std::string data_dir();
void set_data_dir(std::string path);

}  // namespace tugaphone
