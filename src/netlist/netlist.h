#pragma once

#include "../model/workspace.h"

#include <map>
#include <string>
#include <vector>

namespace logic::netlist {

// something a net connects to: a port of an instance or of the cell itself
struct terminal {
    int instance = -1; // index into the schematic's instances, -1 for the cell's own ports
    int port = 0; // index into the ports of the instance's interface or of the cell

    bool operator==(const terminal&) const = default;
};

// wires and terminals that are connected. wires connect at their endpoints,
// terminals where their pin (or port connection point) touches a wire end
// or another terminal.
struct net {
    std::vector<terminal> terminals;
    std::vector<model::id> wires;

    // bit width of the connected ports, 0 if no port is connected
    int width = 0;
    // ports of different widths are connected
    bool width_error = false;
};

struct netlist {
    std::vector<net> nets;

    std::map<model::id, int> wire_net;
    // [instance][port] -> net, -1 if the pin is not connected
    std::vector<std::vector<int>> instance_nets;
    // the cell's own ports -> net, -1 if not connected
    std::vector<int> port_nets;

    std::vector<std::string> problems;

    int net_of_wire(model::id w) const;
};

// connectivity of a schematic cell, using the interfaces of the instances
netlist extract(const model::workspace& ws, const model::cell& c);

}
