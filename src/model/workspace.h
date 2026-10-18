#pragma once

#include "library.h"
#include "primitive.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace logic::model {

// ports and symbol of an instance, see workspace::interface_of
struct interface_view {
    const std::vector<port>* ports = nullptr;
    const model::symbol* symbol = nullptr;

    explicit operator bool() const { return symbol != nullptr; }
};

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

    // makes the primitives available and adds the read only library with a
    // cell for each of them. the registry must outlive the workspace.
    library& add_builtin_library(const primitive_registry& r, const std::string& name = "builtin");

    // the primitive implementing the cell, null for schematics and unknown kinds
    const primitive* primitive_of(const cell& c) const;

    // ports and symbol of the placed instance: built from its parameters for
    // primitives, the cell's own otherwise. empty if the cell is missing. the
    // pointers stay valid until the cell is changed or deleted.
    interface_view interface_of(const instance& i) const;

    // parameters of the instance, empty for cells that are not primitives
    std::vector<param_def> params_of(const instance& i) const;

private:
    const primitive_registry* primitives = nullptr;

    // built interfaces of primitives by kind and parameters. entries are never
    // removed, there are only a few configurations in use.
    mutable std::map<std::string, cell_interface> interface_cache;
};

}
