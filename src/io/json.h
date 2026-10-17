#pragma once

#include "../model/cell.h"

#include <nlohmann/json.hpp>

namespace logic::io {

// version of the cell file format, stored in every file
constexpr int format_version = 1;

nlohmann::json to_json(const model::cell& c);

// throws std::runtime_error (or nlohmann::json::exception) on invalid input
model::cell cell_from_json(const nlohmann::json& j);

std::string id_to_string(model::id i);
model::id id_from_string(const std::string& s);

}
