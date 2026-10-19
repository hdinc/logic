#pragma once

#include "../cosim/component.h"
#include "job.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace logic::hdl {

// iverilog's "file:line: error: message" lines
std::vector<diagnostic> parse_icarus_messages(const std::string& output);

struct icarus_tools {
    std::filesystem::path iverilog, vvp, iverilog_vpi;
    // first line of `iverilog -V`
    std::string version;
    // why it can't be used, empty if it can
    std::string error;

    bool ok() const { return error.empty(); }
};

// iverilog, vvp and iverilog-vpi on PATH
icarus_tools find_icarus();

// the folder with the vpi bridge (logic_vpi.vpi), built into the cache with
// iverilog-vpi when first needed
bool icarus_bridge(const icarus_tools& tools, const std::filesystem::path& cache_root, std::filesystem::path& dir, std::string& error,
    const std::function<void(const std::string&)>& on_line = {});

// ports and parameters of the top for the job's parameter values, found
// by running the design with the bridge
bool icarus_describe(const icarus_tools& tools, const job& j, const std::filesystem::path& cache_root, std::vector<model::port>& ports,
    std::vector<model::param_def>& params, std::string& error, std::vector<diagnostic>* diagnostics = nullptr);

// the testbench around the top that the bridge drives, see logic_vpi.c
std::string icarus_testbench(const job& j, const std::vector<model::port>& ports);

// hash of everything that changes the compiled design
std::string icarus_build_key(const icarus_tools& tools, const job& j);

// the compiled design of a build and the ports it was built with
struct icarus_design {
    std::filesystem::path vvp;
    std::vector<model::port> ports;
};

// where a build is, whether it's built or not
std::filesystem::path icarus_design_path(const std::filesystem::path& cache_root, const std::string& key);

// compiles the design with the testbench unless it's in the cache
bool icarus_build(const icarus_tools& tools, const job& j, const std::filesystem::path& cache_root, std::filesystem::path& design,
    std::string& error, std::vector<diagnostic>* diagnostics = nullptr, const std::function<void(const std::string&)>& on_line = {});

// a build from the cache, false if it isn't there
bool icarus_load(const std::filesystem::path& design, icarus_design& out);

// how to simulate a built design, in the library folder dir
cosim::launch icarus_launch(const icarus_tools& tools, const std::filesystem::path& design, const std::filesystem::path& bridge_dir,
    const std::filesystem::path& dir);

}
