#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace logic::hdl {

// extra environment variables for a child process
using environment = std::vector<std::pair<std::string, std::string>>;

// a running program. stdout and stderr go to one pipe, stdin is empty.
class child_process {
public:
    child_process() = default;
    // kills it if it still runs
    ~child_process();

    child_process(const child_process&) = delete;
    child_process& operator=(const child_process&) = delete;

    // args[0] is the program, found on PATH if it has no directory. false
    // with an error if it can't be started.
    bool start(const std::vector<std::string>& args, const std::filesystem::path& dir, const environment& env, std::string& error);

    // reads the output until the program closes it, each line goes to
    // on_line. blocks.
    void read_output(const std::function<void(const std::string&)>& on_line, std::string* all = nullptr);

    // waits for the end, the exit code or -1 if it was killed
    int wait();
    // false once it ended
    bool running();
    void kill();

private:
#ifdef _WIN32
    void* process = nullptr;
    void* output = nullptr;
#else
    int pid = -1;
    int output = -1;
    // waitpid returned, the output may still be open
    bool reaped = false;
#endif
    bool waited = false;
    int exit_code = -1;
};

struct process_result {
    // -1 if it couldn't be started or was killed
    int exit_code = -1;
    // stdout and stderr together
    std::string output;
    // why it couldn't be started
    std::string error;

    bool ok() const { return exit_code == 0; }
};

// runs a program and waits for it. each line of output is passed to on_line
// as it comes.
process_result run_process(const std::vector<std::string>& args, const std::filesystem::path& dir = {},
    const std::function<void(const std::string&)>& on_line = {}, const environment& env = {});

// the program on PATH, empty if not found
std::filesystem::path find_program(const std::string& name);

}
