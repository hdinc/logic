#pragma once

#include "external.h"
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
    // never null when the view is valid, empty for schematic cells
    const std::vector<bit_link>* links = nullptr;
    // never null when the view is valid, empty for schematic cells
    const std::vector<pull>* pulls = nullptr;

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

    // creates a cell with a fresh id, an empty schematic by default
    cell& create_cell(library& lib, const std::string& name, implementation impl = schematic{});
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
    // primitives, the cell's own otherwise. external cells with other than
    // the default parameter values get their ports from the provider and
    // keep their symbol if the port names match. empty if the cell is
    // missing. the pointers stay valid until the cell is changed or deleted.
    interface_view interface_of(const instance& i) const;

    // parameters of the instance, empty for cells that are not primitives or
    // external
    std::vector<param_def> params_of(const instance& i) const;

    // a problem with the instance's parameters to show to the user, empty if
    // there is none
    std::string check_instance(const instance& i) const;

    // finds the interfaces of external cells, must outlive the workspace
    void add_provider(const external_provider& p);
    // null if no provider handles the cell
    const external_provider* provider_of(const cell& c) const;

    // asks the provider for the ports and parameters of an external cell and
    // updates it, see cell::set_ports. false with an error if it failed.
    bool refresh_interface(cell& c, std::string& error);

private:
    const primitive_registry* primitives = nullptr;

    // built interfaces of primitives by kind and parameters. entries are never
    // removed, there are only a few configurations in use.
    mutable std::map<std::string, cell_interface> interface_cache;

    // interfaces of external cells with other parameters, by cell and
    // parameters. rebuilt when the cell's ports or symbol changed since.
    struct external_entry {
        cell_interface iface;
        std::vector<port> cell_ports;
        model::symbol cell_symbol;
        std::string error;
    };
    mutable std::map<std::string, external_entry> external_cache;
    const external_entry* external_interface(const cell& c, const instance& i) const;

    std::vector<const external_provider*> providers;
};

}
