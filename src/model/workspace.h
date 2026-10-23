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
    // never null when the view is valid, see cell_interface::model_ports
    const std::vector<model_port>* model_ports = nullptr;

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
    const library* library_of(id cell) const;

    // creates a cell with a fresh id, an empty schematic by default
    cell& create_cell(library& lib, const std::string& name, implementation impl = schematic{});
    void delete_cell(id cell);

    // cells that place an instance of the given cell
    std::vector<cell*> users_of(id cell);

    // true if placing an instance of `used` inside `user` would create a cycle
    bool would_create_cycle(id user, id used) const;

    // checks that a cell name is usable as a file name and unique in lib
    static bool valid_cell_name(const library& lib, const std::string& name, std::string& error);

    // bundle types, see bundle.h. their ids are unique in the workspace,
    // like the ids of cells.
    bundle_type* find_bundle(id i);
    const bundle_type* find_bundle(id i) const;
    library* library_of_bundle(id bundle);
    const library* library_of_bundle(id bundle) const;
    bundle_type& create_bundle(library& lib, const std::string& name);
    // ports and fields of the type keep its id, they are reported as
    // missing a type
    void delete_bundle(id bundle);
    // the same rules as cell names, unique among the bundles of lib
    static bool valid_bundle_name(const library& lib, const std::string& name, std::string& error);

    // what uses the bundle type: ports, fields of other bundles and
    // instances (e.g. breakouts) whose parameters name it, as text
    std::vector<std::string> bundle_users(id bundle) const;

    // true if a field of type `inner` in `outer` would make a bundle contain
    // itself
    bool would_create_bundle_cycle(id outer, id inner) const;

    // the fields of the bundle type flattened for the view
    bundle_layout layout_of(id bundle, bool flipped = false) const;
    // "axi_lite" or "~axi_lite", the id if the type is missing
    std::string bundle_label(id bundle, bool flipped = false) const;

    // call after a bundle type changed: updates the widths of the ports of
    // that type and the port groups of external cells (marking their
    // libraries dirty) and rebuilds the interfaces of primitives that show
    // bundles
    void bundles_changed();

    // the model's ports (leaves) with the port groups applied: the grouped
    // ports replace their fields, at the place of the first one. a group
    // whose fields aren't all there with their width and direction is
    // left out with a problem.
    struct grouping {
        std::vector<port> ports;
        // per leaf: the port it's part of
        std::vector<model_port> model_ports;
        std::vector<std::string> problems;
    };
    grouping group_ports(const std::vector<port>& leaves, const std::vector<port_group>& groups) const;
    // sets the ports of an external cell from its model's ports (the leaf
    // ports, or the ports if there are none yet) and its groups, see
    // cell::set_ports. returns the problems of the grouping.
    std::vector<std::string> apply_groups(cell& c);
    // increased by bundles_changed
    uint64_t bundle_revision() const { return m_bundle_revision; }

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
        uint64_t revision = 0;
        std::string error;
    };
    mutable std::map<std::string, external_entry> external_cache;
    const external_entry* external_interface(const cell& c, const instance& i) const;

    std::vector<const external_provider*> providers;

    uint64_t m_bundle_revision = 0;

    // the model ports of external cells with groups and default
    // parameters, rebuilt when the cell's ports changed
    struct group_entry {
        std::vector<port> ports;
        std::vector<port> leaves;
        uint64_t revision = 0;
        std::vector<model_port> model_ports;
    };
    mutable std::map<id, group_entry> group_cache;
    // the model ports for the cell's ports from its leaves, by port name
    std::vector<model_port> map_model_ports(const std::vector<port>& ports, const std::vector<port>& leaves, const std::vector<port_group>& groups) const;
};

}
