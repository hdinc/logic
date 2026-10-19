#pragma once

#include "../cosim/component.h"
#include "job.h"
#include "vhdl_parse.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace logic::hdl {

// ghdl's "file:line:col:error: message" lines
std::vector<diagnostic> parse_ghdl_messages(const std::string& output);

struct ghdl_tools {
    std::filesystem::path ghdl;
    // the c compiler for the bridge and ghdl's vpi header and library
    std::string cc;
    std::filesystem::path include_dir, lib_dir;
    // first line of `ghdl --version`
    std::string version;
    std::string error;

    bool ok() const { return error.empty(); }
};

// ghdl and a c compiler ($CC, cc or gcc) on PATH
ghdl_tools find_ghdl();

// the folder with the vpi bridge built against ghdl's vpi library
bool ghdl_bridge(const ghdl_tools& tools, const std::filesystem::path& cache_root, std::filesystem::path& dir, std::string& error,
    const std::function<void(const std::string&)>& on_line = {});

// the top entity in the job's sources
bool ghdl_entity(const job& j, vhdl_entity& entity, std::string& error);

// parameters (generics) of the top as the program uses them
std::vector<model::param_def> ghdl_params(const vhdl_entity& entity);

// ports and parameters for the job's parameter values: generics from the
// source, ports from running the design with the bridge
bool ghdl_describe(const ghdl_tools& tools, const job& j, const std::filesystem::path& cache_root, std::vector<model::port>& ports,
    std::vector<model::param_def>& params, std::string& error, std::vector<diagnostic>* diagnostics = nullptr);

// the testbench around the top that the bridge drives. it has the top's
// generics, so one build works for all parameter values.
std::string ghdl_testbench(const vhdl_entity& entity, std::string& error);

// hash of the sources and settings, not the parameters
std::string ghdl_build_key(const ghdl_tools& tools, const job& j);

// the analysed design of a build (a ghdl work folder), whether it's built
// or not
std::filesystem::path ghdl_work_path(const std::filesystem::path& cache_root, const std::string& key);
// the build finished
bool ghdl_is_built(const std::filesystem::path& work);

bool ghdl_build(const ghdl_tools& tools, const job& j, const std::filesystem::path& cache_root, std::filesystem::path& work, std::string& error,
    std::vector<diagnostic>* diagnostics = nullptr, const std::function<void(const std::string&)>& on_line = {});

// how to simulate a build with the job's parameter values, in the
// library folder
cosim::launch ghdl_launch(const ghdl_tools& tools, const job& j, const std::filesystem::path& work, const std::filesystem::path& bridge_dir);

}
