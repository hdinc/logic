#pragma once

#include <string>
#include <unordered_map>

#include "circuit.h"

namespace logic::library {

class library {
public:

    std::unordered_map<std::string, circuit> circuits;
    std::string name;

};

}