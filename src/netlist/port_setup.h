#pragma once

#include "netlist.h"

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace logic::netlist {

// a name new ports get (in1, out2, io3, see cell::add_port): the user hasn't
// named the port
bool default_port_name(const std::string& name);

// the instance pins (instance id, port index) each of the cell's ports is
// connected to, by port id
std::map<model::id, std::set<std::pair<model::id, int>>> linked_pins(const model::cell& c, const netlist& nl);

// gives the listed ports of the cell the settings of the instance pins
// their nets reach, where a port still has its defaults:
//   - plain 1 bit: width, bundle type and view, if all the pins agree
//   - a default name: the pin's name, made unique, if there is one pin
//   - no description: the pin's, if there is one pin
// pins that take the width of their net (e.g. a tunnel's) don't count.
// true if a port changed.
bool adopt_pin_settings(const model::workspace& ws, model::cell& c, const std::vector<model::id>& ports);

// for each pin of the instance (index into the schematic's instances) that
// isn't connected: a port of the pin's direction, name, width, type and
// description, two grids out from the pin's side of the symbol, and a wire
// to it. returns the ids of the new ports.
std::vector<model::id> add_ports_for_pins(const model::workspace& ws, model::cell& c, int instance);

}
