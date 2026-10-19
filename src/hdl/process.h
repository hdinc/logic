#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace logic::hdl {

struct process_result {
    // -1 if it couldn't be started or was killed
    int exit_code = -1;
    // stdout and stderr together
    std::string output;
    // why it couldn't be started
    std::string error;

    bool ok() const { return exit_code == 0; }
};

// runs a program and waits for it. args[0] is the program, found on PATH if
// it has no directory. each line of output is passed to on_line as it comes.
process_result run_process(const std::vector<std::string>& args, const std::filesystem::path& dir = {},
    const std::function<void(const std::string&)>& on_line = {});

// the program on PATH, empty if not found
std::filesystem::path find_program(const std::string& name);

}
