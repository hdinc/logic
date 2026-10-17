#pragma once

#include <string>
#include <glm/glm.hpp>

namespace logic::library {

struct component {
    glm::ivec2 position;

    std::string library_name;
    std::string name;
};

}