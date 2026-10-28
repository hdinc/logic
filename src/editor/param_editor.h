#pragma once

#include "../model/param.h"
#include "../model/workspace.h"

#include <filesystem>
#include <map>
#include <vector>

namespace logic::editor {

// the category of a cell: a text input and a list of the library's
// categories to pick from. returns true if it changed.
bool edit_category(const model::library& lib, std::string& category);

// imgui widgets for the parameters, returns true if a value changed. only
// values that differ from the defaults are kept in values. file parameters
// get a button for a file dialog, the chosen file is relative to base when
// it's inside it. with a workspace, bundle parameters get a list of the
// bundle types and field parameters a list of the fields of their bundle
// (if that parameter is among defs).
// with exprs, integer parameters are text inputs that take a number or an
// expression of the parameters in scope: a number goes into values, an
// expression into exprs (and its value into values only later, see
// workspace::apply_param_defaults).
bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base = {}, const model::workspace* ws = nullptr,
    std::map<std::string, std::string>* exprs = nullptr, const model::param_values* scope = nullptr);

// a text input for an integer expression of the parameters in scope (see
// model::eval_expr). while it can't be evaluated it's in the warning color
// with the reason as its tooltip, otherwise the tooltip is given one. empty
// text shows the hint. returns true if the text changed.
bool edit_expr(const char* label, std::string& text, const model::param_values& scope, const char* hint = nullptr, const std::string& tooltip = {});

// a table of integer parameters to define them: name, default, min, max,
// description, reordering and removing, and a button to add one. returns
// true if changed.
bool edit_int_params(std::vector<model::param_def>& params, const char* none);
}
