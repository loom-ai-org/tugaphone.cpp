// The command-line records run the tugaphone executable that TUGAPHONE_CLI
// names, in the directory TUGAPHONE_CLI_CWD names, as separate processes.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace {

struct Result {
    std::string out, err;
    int status = -1;
};

std::string required_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') throw std::runtime_error(std::string(name) + " is not set");
    return value;
}

// Run `binary args...` in `cwd` with COLUMNS set to `columns` or unset.
Result run(const std::string& binary, const std::string& cwd, const std::vector<std::string>& args,
           const std::optional<std::string>& columns) {
    // Everything the child needs is built before fork: only async-signal-safe
    // calls run between fork and exec.
    std::vector<std::string> env;
    for (char** e = environ; *e != nullptr; ++e)
        if (std::strncmp(*e, "COLUMNS=", 8) != 0) env.emplace_back(*e);
    if (columns) env.push_back("COLUMNS=" + *columns);
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<std::string> owned{binary};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) throw std::runtime_error("pipe failed");
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
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

// The class in the last line of a Python traceback: "json.decoder.JSONDecodeError: ..." -> its name.
std::string exception_class(const std::string& text) {
    std::string body = text;
    if (!body.empty() && body.back() == '\n') body.pop_back();
    const std::string last = body.substr(body.rfind('\n') == std::string::npos ? 0 : body.rfind('\n') + 1);
    return last.substr(0, last.find(':'));
}

}  // namespace

std::string record_cli(const Value& v) {
    const std::string binary = required_env("TUGAPHONE_CLI");
    const std::string cwd = required_env("TUGAPHONE_CLI_CWD");
    const Result r = run(binary, cwd, v[1].strings(), opt_str(v[2]));
    const bool traceback = v[6].boolean;
    std::string err = r.err;
    // A traceback names Python files; the C++ error line must name the same class.
    if (traceback && r.err.find('\n') == r.err.size() - 1 && exception_class(r.err) == v[4].str)
        err = v[4].str;
    return list({J("cli"), json_line::dump(v[1]), json_line::dump(v[2]), J(r.out), J(err), J(r.status),
                 json_line::dump(v[6])});
}

}  // namespace parity
