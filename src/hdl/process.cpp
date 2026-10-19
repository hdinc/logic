#include "process.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace logic::hdl {

namespace fs = std::filesystem;

namespace {

// splits output into lines for the callback
class line_splitter {
public:
    line_splitter(std::string* all, const std::function<void(const std::string&)>& f)
        : all(all)
        , f(f)
    {
    }

    void add(const char* data, size_t n)
    {
        if (all) all->append(data, n);
        if (!f) return;
        for (size_t k = 0; k < n; k++) {
            if (data[k] == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                f(line);
                line.clear();
            } else {
                line += data[k];
            }
        }
    }

    void finish()
    {
        if (f && !line.empty()) f(line);
    }

private:
    std::string* all;
    const std::function<void(const std::string&)>& f;
    std::string line;
};

}

fs::path find_program(const std::string& name)
{
    if (fs::path(name).has_parent_path()) return fs::exists(name) ? fs::path(name) : fs::path{};
    auto path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    const char sep = ';';
    const char* exts[] = {".exe", ".bat", ".cmd", ""};
#else
    const char sep = ':';
    const char* exts[] = {""};
#endif
    std::string s = path;
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        if (end > start) {
            for (auto ext : exts) {
                auto p = fs::path(s.substr(start, end - start)) / (name + ext);
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) {
#ifndef _WIN32
                    if (access(p.c_str(), X_OK) != 0) continue;
#endif
                    return p;
                }
            }
        }
        start = end + 1;
    }
    return {};
}

child_process::~child_process()
{
    kill();
    wait();
}

#ifdef _WIN32

namespace {

// quoting for CreateProcess command lines
std::wstring quote(const std::wstring& a)
{
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    int slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        if (c == L'"') r.append(slashes * 2 + 1, L'\\');
        else r.append(slashes, L'\\');
        slashes = 0;
        r += c;
    }
    r.append(slashes * 2, L'\\');
    return r + L"\"";
}

std::wstring widen(const std::string& s)
{
    return fs::path(s).wstring();
}

// the current environment with the extra variables, as CreateProcessW wants it
std::wstring environment_block(const environment& env)
{
    std::vector<std::wstring> vars;
    auto block = GetEnvironmentStringsW();
    for (auto p = block; *p; p += wcslen(p) + 1)
        vars.push_back(p);
    FreeEnvironmentStringsW(block);
    for (auto& [k, v] : env) {
        auto key = widen(k) + L"=";
        std::erase_if(vars, [&](auto& x) { return _wcsnicmp(x.c_str(), key.c_str(), key.size()) == 0; });
        vars.push_back(key + widen(v));
    }
    std::wstring r;
    for (auto& v : vars)
        r += v + L'\0';
    return r + L'\0';
}

}

bool child_process::start(const std::vector<std::string>& args, const fs::path& dir, const environment& env, std::string& error)
{
    if (args.empty()) {
        error = "nothing to run";
        return false;
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        error = "can't create a pipe";
        return false;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd;
    for (auto& a : args)
        cmd += (cmd.empty() ? L"" : L" ") + quote(widen(a));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = nul;
    PROCESS_INFORMATION pi{};
    auto wdir = dir.empty() ? std::wstring() : dir.wstring();
    auto block = environment_block(env);
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
        wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    CloseHandle(write_end);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) {
        CloseHandle(read_end);
        error = "can't start " + args[0] + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    output = read_end;
    waited = false;
    return true;
}

void child_process::read_output(const std::function<void(const std::string&)>& on_line, std::string* all)
{
    if (!output) return;
    line_splitter lines(all, on_line);
    char buf[4096];
    DWORD n;
    while (ReadFile((HANDLE)output, buf, sizeof(buf), &n, nullptr) && n > 0)
        lines.add(buf, n);
    lines.finish();
}

bool child_process::running()
{
    return process && !waited && WaitForSingleObject((HANDLE)process, 0) == WAIT_TIMEOUT;
}

int child_process::wait()
{
    if (!process || waited) return exit_code;
    WaitForSingleObject((HANDLE)process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess((HANDLE)process, &code);
    CloseHandle((HANDLE)process);
    if (output) CloseHandle((HANDLE)output);
    process = output = nullptr;
    waited = true;
    exit_code = (int)code;
    return exit_code;
}

void child_process::kill()
{
    if (process && !waited) TerminateProcess((HANDLE)process, 1);
}

#else

bool child_process::start(const std::vector<std::string>& args, const fs::path& dir, const environment& env, std::string& error)
{
    if (args.empty()) {
        error = "nothing to run";
        return false;
    }
    auto exe = find_program(args[0]);
    if (exe.empty()) {
        error = "can't start " + args[0] + ": not found";
        return false;
    }

    // everything the child needs is prepared before fork
    std::vector<std::string> env_strings;
    for (char** e = environ; *e; e++) {
        std::string s = *e;
        auto key = s.substr(0, s.find('='));
        bool replaced = false;
        for (auto& [k, v] : env)
            replaced = replaced || k == key;
        if (!replaced) env_strings.push_back(s);
    }
    for (auto& [k, v] : env)
        env_strings.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& s : env_strings)
        envp.push_back(s.data());
    envp.push_back(nullptr);

    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    std::string exe_str = exe.string(), dir_str = dir.string();

    int fds[2], err_fds[2];
    if (pipe(fds) != 0) {
        error = "can't create a pipe";
        return false;
    }
    if (pipe(err_fds) != 0) {
        close(fds[0]);
        close(fds[1]);
        error = "can't create a pipe";
        return false;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(err_fds[1], F_SETFD, FD_CLOEXEC);

    pid_t p = fork();
    if (p < 0) {
        error = "can't start " + args[0];
        return false;
    }
    if (p == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        close(err_fds[0]);
        if (!dir_str.empty() && chdir(dir_str.c_str()) != 0) {
            int e = errno;
            (void)!write(err_fds[1], &e, sizeof(e));
            _exit(127);
        }
        execve(exe_str.c_str(), argv.data(), envp.data());
        int e = errno;
        (void)!write(err_fds[1], &e, sizeof(e));
        _exit(127);
    }

    close(fds[1]);
    close(err_fds[1]);
    int exec_errno = 0;
    ssize_t n;
    while ((n = read(err_fds[0], &exec_errno, sizeof(exec_errno))) < 0 && errno == EINTR) {
    }
    close(err_fds[0]);
    if (n == sizeof(exec_errno)) {
        close(fds[0]);
        int status;
        while (waitpid(p, &status, 0) < 0 && errno == EINTR) {
        }
        error = "can't start " + args[0] + ": " + std::strerror(exec_errno);
        return false;
    }

    pid = p;
    output = fds[0];
    waited = false;
    reaped = false;
    return true;
}

void child_process::read_output(const std::function<void(const std::string&)>& on_line, std::string* all)
{
    if (output < 0) return;
    line_splitter lines(all, on_line);
    char buf[4096];
    ssize_t n;
    while ((n = read(output, buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        lines.add(buf, (size_t)n);
    }
    lines.finish();
}

bool child_process::running()
{
    if (pid < 0 || reaped) return false;
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) != pid) return true;
    reaped = true;
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return false;
}

int child_process::wait()
{
    if (pid < 0 || waited) return exit_code;
    if (!reaped) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        reaped = true;
    }
    if (output >= 0) close(output);
    output = -1;
    pid = -1;
    waited = true;
    return exit_code;
}

void child_process::kill()
{
    if (pid > 0 && !reaped) ::kill(pid, SIGKILL);
}

#endif

process_result run_process(const std::vector<std::string>& args, const fs::path& dir, const std::function<void(const std::string&)>& on_line,
    const environment& env)
{
    process_result r;
    child_process c;
    if (!c.start(args, dir, env, r.error)) return r;
    c.read_output(on_line, &r.output);
    r.exit_code = c.wait();
    return r;
}

}
