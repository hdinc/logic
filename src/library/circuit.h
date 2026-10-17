#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../editor/symbol.h"
#include "port.h"
#include "component.h"
#include "wire.h"

namespace logic::library {

struct circuit {
    std::string name;
    std::vector<wire> wires;
    std::vector<component> components;
    std::vector<port> ports;
    editor::symbol::symbol symbol;
};

}