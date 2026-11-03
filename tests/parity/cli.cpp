// The "cli" records run the tugaphone executable that TUGAPHONE_CLI names, in
// the directory TUGAPHONE_CLI_CWD names, as separate processes. The other
// records run in this process and compare the line an uncaught exception
// prints.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

#include "common.hpp"
#include "modules.hpp"
#include "pyio.hpp"
#include "tugaphone/accent.hpp"

namespace parity {

namespace {

struct Result {
    std::string out, err;
    int status = -1;
};

// The last item of a "cli" record: {"env": {...}, "stdout": path, "pty": columns}.
struct Options {
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    std::optional<std::string> stdout_path;
    std::optional<int> pty_columns;
};

Options options_of(const Value& v) {
    Options o;
    if (v.size() < 8) return o;
    for (const auto& [key, value] : v[7].members) {
        if (key == "env") {
            for (const auto& [name, setting] : value.members) o.env.emplace_back(name, opt_str(setting));
        } else if (key == "stdout") {
            o.stdout_path = value.str;
        } else if (key == "pty") {
            o.pty_columns = static_cast<int>(value.integer);
        } else {
            throw std::runtime_error("unknown cli option " + key);
        }
    }
    return o;
}

std::string unhex(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

// The arguments: a string, or {"hex": "<bytes>"} for bytes that are not UTF-8.
std::vector<std::string> arguments(const Value& v) {
    std::vector<std::string> out;
    for (const Value& a : v.items) out.push_back(a.kind == Value::Object ? unhex(a.members.at(0).second.str) : a.str);
    return out;
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}

// The line pyio::print_exception writes for the exception fn raises, or "".
std::string last_line(const std::function<void()>& fn) {
    try {
        fn();
    } catch (...) {
        std::ostringstream out, err;
        tugaphone::detail::pyio::print_exception(out, err);
        std::string line = err.str();
        if (!line.empty() && line.back() == '\n') line.pop_back();
        return line;
    }
    return "";
}

std::string required_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') throw std::runtime_error(std::string(name) + " is not set");
    return value;
}

// Run `binary args...` in `cwd` with COLUMNS set to `columns` or unset.
Result run(const std::string& binary, const std::string& cwd, const std::vector<std::string>& args,
           const std::optional<std::string>& columns, const Options& options) {
    // Everything the child needs is built before fork: only async-signal-safe
    // calls run between fork and exec.
    auto overridden = [&](const char* entry) {
        if (std::strncmp(entry, "COLUMNS=", 8) == 0) return true;
        for (const auto& [name, value] : options.env) {
            if (std::strncmp(entry, name.c_str(), name.size()) == 0 && entry[name.size()] == '=') return true;
        }
        return false;
    };
    std::vector<std::string> env;
    for (char** e = environ; *e != nullptr; ++e)
        if (!overridden(*e)) env.emplace_back(*e);
    if (columns) env.push_back("COLUMNS=" + *columns);
    for (const auto& [name, value] : options.env)
        if (value) env.push_back(name + "=" + *value);
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<std::string> owned{binary};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);
    // Standard output: a pipe, a file opened in the child, or a raw-mode
    // pseudo-terminal of the given width.
    int out_pipe[2], err_pipe[2];
    if (options.pty_columns) {
        out_pipe[0] = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (out_pipe[0] < 0 || grantpt(out_pipe[0]) != 0 || unlockpt(out_pipe[0]) != 0)
            throw std::runtime_error("posix_openpt failed");
        out_pipe[1] = open(ptsname(out_pipe[0]), O_RDWR | O_NOCTTY | O_CLOEXEC);
        termios mode{};
        if (out_pipe[1] < 0 || tcgetattr(out_pipe[1], &mode) != 0) throw std::runtime_error("pty slave failed");
        cfmakeraw(&mode);
        winsize size{};
        size.ws_row = 24;
        size.ws_col = static_cast<unsigned short>(*options.pty_columns);
        if (tcsetattr(out_pipe[1], TCSANOW, &mode) != 0 || ioctl(out_pipe[1], TIOCSWINSZ, &size) != 0)
            throw std::runtime_error("pty setup failed");
    } else if (pipe(out_pipe) != 0) {
        throw std::runtime_error("pipe failed");
    }
    if (pipe(err_pipe) != 0) throw std::runtime_error("pipe failed");
    const char* stdout_path = options.stdout_path ? options.stdout_path->c_str() : nullptr;
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        if (stdout_path != nullptr) {
            const int fd = open(stdout_path, O_WRONLY);
            if (fd < 0) _exit(125);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else {
            dup2(out_pipe[1], STDOUT_FILENO);
        }
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (chdir(cwd.c_str()) != 0) _exit(126);
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }
    close(out_pipe[1]);
    close(err_pipe[1]);
    Result r;
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    std::string* sinks[2] = {&r.out, &r.err};
    int open_fds = 2;
    char buffer[65536];
    while (open_fds > 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("poll failed");
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t n = read(fds[i].fd, buffer, sizeof buffer);
            if (n > 0) {
                sinks[i]->append(buffer, static_cast<size_t>(n));
            } else {
                close(fds[i].fd);
                fds[i].fd = -1;
                --open_fds;
            }
        }
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) throw std::runtime_error("waitpid failed");
    r.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return r;
}

}  // namespace

std::string record_cli(const Value& v) {
    namespace pyio = tugaphone::detail::pyio;
    const std::string& fn = v[0].str;
    if (fn == "overlay_json")
        return head(v) + J(last_line([&] { tugaphone::accent::AccentOverlay::from_json(v[1].str); })) + "]";
    if (fn == "decode") return head(v) + J(last_line([&] { pyio::check_utf8(unhex(v[1].str)); })) + "]";
    if (fn == "transform_error") {
        tugaphone::accent::Transform t;
        t.pattern = v[1].str;
        t.replacement = v[2].str;
        return head(v) + J(last_line([&] { t.apply("abc vinho"); })) + "]";
    }
    const std::string binary = required_env("TUGAPHONE_CLI");
    const std::string cwd = required_env("TUGAPHONE_CLI_CWD");
    const Result r = run(binary, cwd, arguments(v[1]), opt_str(v[2]), options_of(v));
    std::string err = r.err;
    // A traceback record holds the last line of the traceback, which the C++
    // executable prints alone, with this repository as "<root>".
    if (v[6].boolean) {
        if (!err.empty() && err.back() == '\n') err.pop_back();
        replace_all(err, cwd, "<root>");
    }
    std::vector<std::string> parts = {J("cli"), json_line::dump(v[1]), json_line::dump(v[2]), J(r.out), J(err),
                                      J(r.status), json_line::dump(v[6])};
    if (v.size() > 7) parts.push_back(json_line::dump(v[7]));
    return list(parts);
}

}  // namespace parity
