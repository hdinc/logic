#pragma once

#include <string>
#include <glm/glm.hpp>

namespace logic::library {

struct port {
    enum class port_type {
        input,
        output,
        inout
    };

    glm::ivec2 position;

    std::string name;
    int number;
    port_type type;
};

}