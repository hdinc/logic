#pragma once

#include "param.h"
#include "types.h"

#include <map>
#include <string>
#include <vector>

namespace logic::model {

// a field of a bundle type: bits with a direction, or another bundle
struct bundle_field {
    std::string name;
    std::string description;

    // bits, used when bundle is no_id. the width is an integer expression
    // over the parameters of the bundle (see expr.h), e.g. "DATA_W / 8". a
    // field of width 0 is left out.
    std::string width = "1";
    direction dir = direction::output;

    // a nested bundle, flipped swaps the directions of all its fields. args
    // are the values of its parameters, expressions over the parameters of
    // this bundle. missing ones take their defaults.
    model::id bundle = no_id;
    bool flipped = false;
    std::map<std::string, std::string> args{};

    bool operator==(const bundle_field&) const = default;
};

// named wires that go together, like an interface in systemverilog. a port
// or wire of a bundle type is one net of the bundle's total width, the
// fields are bit ranges in it. directions are those of the normal view,
// the flipped view swaps inputs and outputs.
struct bundle_type {
    model::id id = no_id;
    std::string name;
    std::string description;
    // integer parameters, e.g. a data width. a use of the bundle (a port, a
    // nested field, a breakout) gives their values, see bundle_args.
    std::vector<param_def> params;
    std::vector<bundle_field> fields;

    const bundle_field* find_field(const std::string& name) const;
    bool operator==(const bundle_type&) const = default;
};

// a field of a flattened bundle: bits [offset, offset + width) of the net.
// sub-bundles are flattened into their fields, the paths join the names
// with '.'
struct bundle_leaf {
    std::string path;
    direction dir = direction::output;
    int offset = 0;
    int width = 1;
    bool operator==(const bundle_leaf&) const = default;
};

// a field or nested bundle of a flattened bundle, in declaration order:
// each node is followed by its children. depth 0 are the bundle's own fields.
struct bundle_node {
    std::string path;
    std::string name;
    int depth = 0;
    int offset = 0;
    int width = 0;
    // set for nested bundles, with the values of all its parameters
    model::id bundle = no_id;
    bool flipped = false;
    param_values args;
    // for bits, in the view the layout was made for
    direction dir = direction::output;
};

// the fields of a bundle type in one view. fields take the bits in
// declaration order from bit 0.
struct bundle_layout {
    std::vector<bundle_leaf> leaves;
    std::vector<bundle_node> nodes;
    int width = 0;
    // a missing type, a bundle containing itself or too many bits. the
    // layout is empty then.
    std::string error;

    explicit operator bool() const { return error.empty(); }
    const bundle_node* find(const std::string& path) const;
};

// the direction in the flipped view
direction flip(direction d);

// "~name" for the flipped view
std::string view_name(const std::string& bundle, bool flipped);

// a bundle type id as a parameter value (hex digits) and back, no_id if the
// text isn't one
std::string bundle_ref(id bundle);
id parse_bundle_ref(const std::string& s);

// the values of a bundle's parameters as text, "DATA_W=32, USER_W=0", and
// back. parsing skips parts that aren't name=integer.
std::string format_bundle_args(const param_values& args);
param_values parse_bundle_args(const std::string& s);

// the paths in a fields parameter: separated by ',', without spaces around
// them, empty ones dropped
std::vector<std::string> split_fields(const std::string& s);
std::string join_fields(const std::vector<std::string>& paths);

}
