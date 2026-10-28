#pragma once

#include "external.h"
#include "library.h"
#include "primitive.h"

#include <map>
#include <memory>
#include <optional>
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
    // see cell_interface::tunnel, null for cells that are not primitives
    const std::string* tunnel = nullptr;

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

    // the fields of the bundle type flattened for the view, with the given
    // values of its parameters (the defaults for missing ones). an error
    // layout if a value is out of range or a width can't be evaluated.
    bundle_layout layout_of(id bundle, bool flipped = false, const param_values& args = {}) const;
    // "axi_lite" or "~axi_lite", the id if the type is missing. values
    // that differ from the defaults follow: "axi_stream<32>" for a bundle
    // with one parameter, "axi_stream<DATA_W=32,USER_W=0>" for more.
    std::string bundle_label(id bundle, bool flipped = false, const param_values& args = {}) const;
    std::string bundle_label(const port& p) const { return bundle_label(p.bundle, p.flipped, p.bundle_args); }

    // the values of all parameters of the bundle: the given integers, the
    // defaults for the others. names the bundle doesn't have are dropped.
    param_values bundle_values(id bundle, const param_values& args) const;
    // only the given values that differ from the defaults, as ports store
    // them
    param_values bundle_args(id bundle, const param_values& args) const;
    // a problem with the values: names the bundle doesn't have, values out
    // of range. empty if there is none.
    std::string check_bundle_args(id bundle, const param_values& args) const;
    // true if the two are the same type: the same bundle with the same
    // values (views aren't compared)
    bool same_bundle(id a, const param_values& a_args, id b, const param_values& b_args) const;
    // the values of a port group's bundle for the model's parameter values,
    // see bundle_args. nullopt with an error if an expression fails.
    std::optional<param_values> group_args(const port_group& g, const param_values& model_values, std::string& error) const;

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
    // the groups' bundle args are evaluated for the model's parameter values.
    grouping group_ports(const std::vector<port>& leaves, const std::vector<port_group>& groups, const param_values& model_values = {}) const;
    // a new bundle type in lib made of the ports with a common prefix: a
    // field per port, the rest of its name, with its width and direction.
    // the type is named after the last part of the prefix (io_jtag_ ->
    // jtag), made unique. returns the group that makes the ports one
    // bundle port, named after the prefix (io_jtag).
    port_group bundle_from_ports(library& lib, const std::vector<port>& ports, const port_prefix& p);

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
    // the generated box of a cell with an automatic symbol (cell::auto_symbol)
    const model::symbol& auto_symbol_of(const cell& c) const;

    // parameters of the instance, empty for cells that are not primitives or
    // external
    std::vector<param_def> params_of(const instance& i) const;

    // a problem with the instance's parameters to show to the user, empty if
    // there is none
    std::string check_instance(const instance& i) const;

    // evaluates the expressions of a schematic cell for the values of its
    // parameters (one for each, see resolve_params): the widths and bundle
    // values of its ports and, unless ports_only, the parameters of its
    // instances. returns the problems, e.g. an unknown name or a width out
    // of range; what fails keeps its value.
    std::vector<std::string> specialize(cell& c, const param_values& values, bool ports_only = false) const;
    // specialize for the defaults: call after changing the parameters or
    // expressions of a schematic cell. returns the problems.
    std::vector<std::string> apply_param_defaults(cell& c) const;

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

    // interfaces of schematic cells with other parameters, by cell and
    // parameters. rebuilt when the cell's ports, symbol, name or parameters
    // changed since.
    struct schematic_entry {
        std::vector<port> ports;
        model::symbol symbol;
        std::vector<std::string> problems;
        std::vector<port> cell_ports;
        model::symbol cell_symbol;
        std::vector<param_def> cell_params;
        std::string cell_name;
        uint64_t revision = 0;
    };
    mutable std::map<std::string, schematic_entry> schematic_cache;
    // null for cells without parameters and instances with the defaults
    const schematic_entry* schematic_interface(const cell& c, const instance& i) const;

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
    // the generated boxes of cells with an automatic symbol, rebuilt when
    // the ports or the name changed
    struct auto_entry {
        std::vector<port> ports;
        std::string name;
        model::symbol symbol;
    };
    mutable std::map<id, auto_entry> auto_symbols;
    // the model ports for the cell's ports from its leaves, by port name
    std::vector<model_port> map_model_ports(const std::vector<port>& ports, const std::vector<port>& leaves, const std::vector<port_group>& groups,
        const param_values& model_values) const;
};

}
