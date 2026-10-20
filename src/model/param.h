#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace logic::model {

// configuration of a placed instance, e.g. the bit width or number of inputs
// of a gate. choices are stored as their string.
using param_value = std::variant<int64_t, bool, std::string>;
using param_values = std::map<std::string, param_value>;

struct param_def {
    enum type_t {
        integer,
        boolean,
        choice,
        text
    };

    std::string name; // key in param_values and the files
    std::string label; // shown in the properties panel
    type_t type = integer;
    param_value def = int64_t(0);

    int64_t min = 0; // integer range
    int64_t max = 0;
    std::vector<std::string> choices;
    // a text that is the path of a file (firmware, memory contents: intel
    // hex, s-records, elf or binary), relative to the library folder
    bool file = false;

    std::string description;

    bool operator==(const param_def&) const = default;
};

param_def int_param(std::string name, std::string label, int64_t def, int64_t min, int64_t max, std::string description = {});
param_def bool_param(std::string name, std::string label, bool def, std::string description = {});
param_def choice_param(std::string name, std::string label, std::vector<std::string> choices, std::string def, std::string description = {});
param_def text_param(std::string name, std::string label, std::string def, std::string description = {});
// a text parameter holding a file path, empty by default
param_def file_param(std::string name, std::string label, std::string description = {});

// a value for every definition: the stored one if it is valid (right type,
// integers in range, a known choice), otherwise the default. values without a
// definition are dropped.
param_values resolve_params(const std::vector<param_def>& defs, const param_values& values);

// stores v for the parameter, or removes it when it equals the default so
// files only contain what differs from the defaults
void set_param(param_values& values, const param_def& def, param_value v);

// typed access to resolved values, throws if the parameter is missing
int64_t get_int(const param_values& v, const std::string& name);
bool get_bool(const param_values& v, const std::string& name);
const std::string& get_string(const param_values& v, const std::string& name);

// unique string for a set of values, used as a cache key
std::string params_key(const param_values& v);

}
