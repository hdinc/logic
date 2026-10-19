#pragma once

#include "../model/cell.h"

#include <filesystem>
#include <string>

namespace logic::hdl {

// a message of an hdl tool about a place in a source file
struct diagnostic {
    // "error" or "warning"
    std::string severity;
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;
};

// what to compile: the cell's settings, its library folder and the
// parameter values (resolved)
struct job {
    model::hdl_impl impl;
    std::filesystem::path dir;
    model::param_values values;
    // shown in messages
    std::string name;
};

// helpers shared by the backends
namespace detail {

std::string first_line(const std::string& s);
std::string read_file(const std::filesystem::path& p);
bool write_file(const std::filesystem::path& p, const std::string& text);
// extra arguments split at spaces, double quotes keep spaces
std::vector<std::string> split_args(const std::string& s);
// p relative to dir unless absolute
std::filesystem::path absolute_in(const std::filesystem::path& dir, const std::string& p);
// a new empty folder in parent
std::filesystem::path temp_folder(const std::filesystem::path& parent, const std::string& prefix);
// 16 hex digits of a 64 bit fnv-1a hash
std::string hash(const std::string& s);
// the values that differ from the defaults, by name
std::vector<std::pair<std::string, model::param_value>> changed_params(const job& j);
// hash input from the contents of the sources and the files in the include
// folders (by name, size and time)
std::string source_fingerprint(const job& j);

// removes a folder when it goes out of scope
struct remove_on_exit {
    std::filesystem::path p;
    ~remove_on_exit();
};

}

}
