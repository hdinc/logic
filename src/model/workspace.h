#pragma once

#include "library.h"

#include <memory>
#include <string>
#include <vector>

namespace logic::model {

class workspace {
public:
    std::vector<std::unique_ptr<library>> libraries;

    library* find_library(const std::string& name);
    library& add_library(const std::string& name);

    cell* find_cell(id i);
    const cell* find_cell(id i) const;
    library* library_of(id cell);

    // creates an empty schematic cell with a fresh id
    cell& create_cell(library& lib, const std::string& name);
    void delete_cell(id cell);

    // cells that place an instance of the given cell
    std::vector<cell*> users_of(id cell);

    // true if placing an instance of `used` inside `user` would create a cycle
    bool would_create_cycle(id user, id used) const;

    // checks that a cell name is usable as a file name and unique in lib
    static bool valid_cell_name(const library& lib, const std::string& name, std::string& error);

    static id random_id();
};

}
