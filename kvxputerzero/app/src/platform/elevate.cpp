#include "elevate.h"

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if !KVX_DESKTOP_BUILD
#include <sys/xattr.h>
#endif

namespace platform {
namespace {

std::string g_password;

const char* sudo_path() {
    for (const char* p : {"/usr/bin/sudo", "/bin/sudo"}) {
        if (::access(p, X_OK) == 0) return p;
    }
    return nullptr;
}

const char* setcap_path() {
    for (const char* p : {"/usr/sbin/setcap", "/sbin/setcap", "/usr/bin/setcap"}) {
        if (::access(p, X_OK) == 0) return p;
    }
    return nullptr;
}

std::vector<std::string> installed_net_helpers() {
    std::vector<std::string> out;
    for (const char* p : {"/usr/lib/kvxputerzero/kvxputerzero-wifimon",
                          "/usr/lib/kvxputerzero/kvxputerzero-blescan"}) {
        if (::access(p, X_OK) == 0) out.emplace_back(p);
    }
    return out;
}

bool file_has_caps(const std::string& path) {
#if KVX_DESKTOP_BUILD
    (void)path;
    return true;
#else
    char buf[256];
    const ssize_t n = ::getxattr(path.c_str(), "security.capability", buf, sizeof(buf));
    return n > 0;
#endif
}

bool sudo_with_password(const std::string& password, const std::vector<std::string>& argv, std::string& output,
                        std::string& error) {
    output.clear();
    error.clear();
    if (password.empty()) {
        error = "password required";
        return false;
    }
    if (password.find('\n') != std::string::npos || password.find('\r') != std::string::npos) {
        error = "password cannot contain a newline";
        return false;
    }
    const char* sudo = sudo_path();
    if (!sudo) {
        error = "sudo is not installed";
        return false;
    }
    if (argv.empty() || argv[0].empty()) {
        error = "no command";
        return false;
    }

    int in_pipe[2]{-1, -1};
    int out_pipe[2]{-1, -1};
    if (::pipe(in_pipe) != 0) {
        error = "pipe failed";
        return false;
    }
    if (::pipe(out_pipe) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        error = "pipe failed";
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "fork failed";
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return false;
    }
    if (pid == 0) {
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(out_pipe[1], STDERR_FILENO);
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);

        std::vector<std::string> args = {"sudo", "-S", "-p", "", "-k", "--"};
        args.insert(args.end(), argv.begin(), argv.end());
        std::vector<char*> av;
        av.reserve(args.size() + 1);
        for (auto& s : args) av.push_back(s.data());
        av.push_back(nullptr);
        ::execv(sudo, av.data());
        _exit(127);
    }

    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    const std::string line = password + "\n";
    size_t off = 0;
    while (off < line.size()) {
        const ssize_t n = ::write(in_pipe[1], line.data() + off, line.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(n);
    }
    ::close(in_pipe[1]);

    char buf[512];
    while (output.size() < 8192) {
        const ssize_t n = ::read(out_pipe[0], buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        output.append(buf, buf + n);
    }
    ::close(out_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = output.empty() ? "sudo rejected the password" : output;
        output.clear();
        return false;
    }
    return true;
}

} // namespace

bool sudo_available() { return sudo_path() != nullptr; }

bool net_helpers_ready() {
#if KVX_DESKTOP_BUILD
    return true;
#else
    if (::geteuid() == 0) return true;
    const auto helpers = installed_net_helpers();
    if (helpers.empty()) return true;
    for (const auto& path : helpers) {
        if (!file_has_caps(path)) return false;
    }
    return true;
#endif
}

bool root_session_ready() {
    if (::geteuid() == 0) return true;
    return !g_password.empty();
}

bool accept_root_password(const std::string& password, std::string& error) {
    std::string output;
    if (!sudo_with_password(password, {"true"}, output, error)) return false;
    g_password = password;
    return true;
}

bool grant_net_helpers(const std::string& password, std::string& error) {
    if (!accept_root_password(password, error)) return false;
#if KVX_DESKTOP_BUILD
    return true;
#else
    const char* setcap = setcap_path();
    if (!setcap) {
        error = "setcap is not installed";
        return false;
    }
    for (const auto& path : installed_net_helpers()) {
        if (file_has_caps(path)) continue;
        std::string output;
        if (!sudo_with_password(password, {setcap, "cap_net_raw,cap_net_admin+ep", path}, output, error)) {
            return false;
        }
        if (!file_has_caps(path)) {
            error = "capabilities were not applied to " + path;
            return false;
        }
    }
    return true;
#endif
}

bool sudo_exec(const std::vector<std::string>& argv, std::string& output, std::string& error) {
    if (g_password.empty() && ::geteuid() != 0) {
        error = "administrator password required";
        return false;
    }
    if (::geteuid() == 0) {
        output.clear();
        error.clear();
        if (argv.empty() || argv[0].empty()) {
            error = "no command";
            return false;
        }
        int out_pipe[2]{-1, -1};
        if (::pipe(out_pipe) != 0) {
            error = "pipe failed";
            return false;
        }
        const pid_t pid = ::fork();
        if (pid < 0) {
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            error = "fork failed";
            return false;
        }
        if (pid == 0) {
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::dup2(out_pipe[1], STDERR_FILENO);
            ::close(out_pipe[0]);
            ::close(out_pipe[1]);
            std::vector<char*> av;
            for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
            av.push_back(nullptr);
            ::execv(argv[0].c_str(), av.data());
            _exit(127);
        }
        ::close(out_pipe[1]);
        char buf[512];
        while (output.size() < 8192) {
            const ssize_t n = ::read(out_pipe[0], buf, sizeof(buf));
            if (n <= 0) break;
            output.append(buf, buf + n);
        }
        ::close(out_pipe[0]);
        int status = 0;
        ::waitpid(pid, &status, 0);
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error = output.empty() ? "command failed" : output;
            return false;
        }
        return true;
    }
    return sudo_with_password(g_password, argv, output, error);
}

} // namespace platform
