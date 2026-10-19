#include "process.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace logic::hdl {

namespace fs = std::filesystem;

namespace {

// splits the output into lines for the callback
class line_splitter {
public:
    line_splitter(std::string& out, const std::function<void(const std::string&)>& f)
        : out(out)
        , f(f)
    {
    }

    void add(const char* data, size_t n)
    {
        out.append(data, n);
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
    std::string& out;
    const std::function<void(const std::string&)>& f;
    std::string line;
};

}

fs::path find_program(const std::string& name)
{
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

}

process_result run_process(const std::vector<std::string>& args, const fs::path& dir, const std::function<void(const std::string&)>& on_line)
{
    process_result r;
    if (args.empty()) {
        r.error = "nothing to run";
        return r;
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        r.error = "can't create a pipe";
        return r;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd;
    for (auto& a : args)
        cmd += (cmd.empty() ? L"" : L" ") + quote(widen(a));

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    auto wdir = dir.empty() ? std::wstring() : dir.wstring();
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        r.error = "can't start " + args[0] + " (error " + std::to_string(GetLastError()) + ")";
        return r;
    }

    line_splitter lines(r.output, on_line);
    char buf[4096];
    DWORD n;
    while (ReadFile(read_end, buf, sizeof(buf), &n, nullptr) && n > 0)
        lines.add(buf, n);
    lines.finish();
    CloseHandle(read_end);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    r.exit_code = (int)code;
    return r;
}

#else

process_result run_process(const std::vector<std::string>& args, const fs::path& dir, const std::function<void(const std::string&)>& on_line)
{
    process_result r;
    if (args.empty()) {
        r.error = "nothing to run";
        return r;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        r.error = "can't create a pipe";
        return r;
    }

    // a pipe to report a failed exec
    int err_fds[2];
    if (pipe(err_fds) != 0) {
        close(fds[0]);
        close(fds[1]);
        r.error = "can't create a pipe";
        return r;
    }
    fcntl(err_fds[1], F_SETFD, FD_CLOEXEC);

    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        r.error = "can't start " + args[0];
        return r;
    }
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        close(err_fds[0]);
        if (!dir.empty() && chdir(dir.c_str()) != 0) {
            int e = errno;
            (void)!write(err_fds[1], &e, sizeof(e));
            _exit(127);
        }
        execvp(argv[0], argv.data());
        int e = errno;
        (void)!write(err_fds[1], &e, sizeof(e));
        _exit(127);
    }

    close(fds[1]);
    close(err_fds[1]);

    line_splitter lines(r.output, on_line);
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        lines.add(buf, (size_t)n);
    }
    lines.finish();
    close(fds[0]);

    int exec_errno = 0;
    bool exec_failed = read(err_fds[0], &exec_errno, sizeof(exec_errno)) == sizeof(exec_errno);
    close(err_fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (exec_failed) {
        r.error = "can't start " + args[0] + ": " + std::strerror(exec_errno);
        return r;
    }
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

#endif

}
