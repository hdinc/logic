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

std::string id_to_string(model::id i);
model::id id_from_string(const std::string& s);

}
