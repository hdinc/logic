#pragma once

#include "../model/workspace.h"
#include "../sim/value.h"

namespace logic::editor {

// a table of the fields of a bundle: name, direction, bits and, with a
// value of the whole bundle, the value of each field. nested bundles indent
// their fields.
void bundle_fields_table(const model::bundle_layout& layout, const sim::value* v = nullptr);

// inputs for the fields of a bundle the user drives: checkboxes for single
// bits, hex inputs for wider fields, z buttons for inouts. fields that are
// outputs of the view show their value. returns true if v changed.
bool edit_bundle_value(const model::bundle_layout& layout, sim::value& v, const sim::value& current);

// an integer input per parameter of the bundle type, labelled with its name.
// args keeps only the values that differ from the defaults, see
// workspace::bundle_args. nothing for a bundle without parameters. returns
// true if args changed.
bool edit_bundle_args(const model::bundle_type& t, model::param_values& args);

}
