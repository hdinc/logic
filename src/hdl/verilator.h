#pragma once

#include "job.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace logic::hdl {

// verilator's "%Error: file:line:col: message" and "%Warning-CODE: ..." lines
std::vector<diagnostic> parse_verilator_messages(const std::string& output);

// the verilator installation
struct verilator_tools {
    // the command to run verilator, e.g. {"perl", ".../verilator"} on windows
    std::vector<std::string> command;
    // first lines of `verilator --version` and of the c++ compiler's
    std::string version;
    std::string cxx_version;
    // why it can't be used, empty if it can
    std::string error;

    bool ok() const { return error.empty(); }
};

// looks for verilator on PATH, or uses LOGIC_VERILATOR
verilator_tools find_verilator();

// ports and parameters of the top for the job's parameter values
bool verilator_describe(const verilator_tools& tools, const job& job, std::vector<model::port>& ports, std::vector<model::param_def>& params,
    std::string& error, std::vector<diagnostic>* diagnostics = nullptr);

// hash of everything that changes the built plugin: tools, settings, the
// contents of the sources and the parameters
std::string verilator_build_key(const verilator_tools& tools, const job& job);

// where the plugin of a build is, whether it's built or not
std::filesystem::path verilator_plugin_path(const std::filesystem::path& cache_root, const std::string& key);

// builds the plugin unless it's in the cache. each line of the tools'
// output goes to on_line.
bool verilator_build(const verilator_tools& tools, const job& job, const std::filesystem::path& cache_root, std::filesystem::path& plugin,
    std::string& error, std::vector<diagnostic>* diagnostics = nullptr, const std::function<void(const std::string&)>& on_line = {});

// c++ source of the wrapper implementing the c model abi around a verilated
// top with the ports in their order. the header of the verilated model
// tells the c types. used by verilator_build, public for tests.
std::string verilator_wrapper(const std::string& top, const std::string& model_header, const std::vector<model::port>& ports, std::string& error);

}
