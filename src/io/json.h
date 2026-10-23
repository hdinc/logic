#pragma once

#include "../model/cell.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace logic::io {

// version of the cell file format, stored in every file
constexpr int format_version = 1;

nlohmann::json to_json(const model::cell& c);

// throws std::runtime_error (or nlohmann::json::exception) on invalid input
model::cell cell_from_json(const nlohmann::json& j);

// copied elements as text for the system clipboard: a cell holding them,
// tagged with what was copied, e.g. "schematic" or "symbol"
std::string to_clipboard(const model::cell& part, const std::string& what);
// null if the text isn't a clipboard of that kind
std::optional<model::cell> from_clipboard(const std::string& text, const std::string& what);

// a symbol for the ports of c from json: a cell file (its symbol, pins
// matched to c's ports by the names of the file's ports) or a symbol whose
// pins name their port:
//   {"shapes": [...], "pins": [{"port": "clk", "pos": [x, y]}]}
// shapes and pins get new ids from c. throws std::runtime_error (or a json
// exception) on invalid input or when a port of c has no pin.
model::symbol symbol_for(model::cell& c, const nlohmann::json& j);

// bundle type files, see storage.h
nlohmann::json to_json(const model::bundle_type& b);
// throws like cell_from_json
model::bundle_type bundle_from_json(const nlohmann::json& j);

std::string id_to_string(model::id i);
model::id id_from_string(const std::string& s);

}
