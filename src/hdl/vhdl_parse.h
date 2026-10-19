#pragma once

#include <string>
#include <vector>

namespace logic::hdl {

// the declaration of a vhdl entity, as far as a testbench needs it. the
// texts are copied from the source without comments.
struct vhdl_entity {
    struct generic {
        std::string name;
        std::string type;
        // empty if there is none
        std::string def;
    };
    struct port {
        std::string name;
        // in, out, inout, buffer or linkage, lower case
        std::string mode;
        std::string type;
    };

    std::string name;
    // the library and use clauses before the entity in its file
    std::string context;
    std::vector<generic> generics;
    std::vector<port> ports;
};

// finds the entity in vhdl source text, ignoring case. false if it isn't
// there, with an error if it is but can't be read.
bool parse_vhdl_entity(const std::string& source, const std::string& name, vhdl_entity& out, std::string& error);

}
