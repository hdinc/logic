#include "param_editor.h"
#include "../model/expr.h"
#include "../ui/theme.h"
#include "bundle_ui.h"
#include "context.h"
#include "file_dialog.h"

#include <algorithm>

namespace logic::editor {

namespace {

// a combo of the bundle types of the workspace, grouped by library
bool edit_bundle(const char* label, std::string& value, const model::workspace& ws)
{
    using namespace ImGui;
    auto id = model::parse_bundle_ref(value);
    auto preview = id == model::no_id ? std::string("(none)") : ws.bundle_label(id);
    bool edited = false;
    if (BeginCombo(label, preview.c_str())) {
        if (Selectable("(none)", id == model::no_id)) {
            value.clear();
            edited = true;
        }
        for (auto& l : ws.libraries) {
            if (l->bundles.empty()) continue;
            SeparatorText(l->name.c_str());
            for (auto& b : l->bundles) {
                PushID(b.get());
                if (Selectable(b->name.c_str(), b->id == id)) {
                    value = model::bundle_ref(b->id);
                    edited = true;
                }
                if (!b->description.empty()) SetItemTooltip("%s", b->description.c_str());
                PopID();
            }
        }
        EndCombo();
    }
    return edited;
}

// the fields of the layout as a tree: checkboxes for several, selectables for
// one field. nested bundles can be chosen as a whole too.
bool edit_fields(const char* label, std::string& value, const model::bundle_layout& layout, bool one)
{
    using namespace ImGui;
    auto paths = model::split_fields(value);
    auto chosen = [&](const std::string& p) { return std::find(paths.begin(), paths.end(), p) != paths.end(); };

    std::string preview = value.empty() ? (one ? "(none)" : "(all fields)") : value;
    bool edited = false;
    if (BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (!one && Selectable("(all fields)", value.empty(), ImGuiSelectableFlags_NoAutoClosePopups)) {
            paths.clear();
            edited = true;
        }
        for (auto& n : layout.nodes) {
            PushID(n.path.c_str());
            // Indent(0) would indent by the default spacing
            float indent = n.depth * GetStyle().IndentSpacing;
            if (indent > 0) Indent(indent);
            auto text = n.name + (n.bundle != model::no_id ? "  (bundle)" : n.width > 1 ? "  [" + std::to_string(n.width) + "]"
                                                                                        : "");
            if (one) {
                if (Selectable(text.c_str(), chosen(n.path))) {
                    paths = {n.path};
                    edited = true;
                }
            } else {
                bool on = chosen(n.path);
                if (Checkbox(text.c_str(), &on)) {
                    if (on)
                        paths.push_back(n.path);
                    else
                        std::erase(paths, n.path);
                    edited = true;
                }
            }
            if (n.bundle == model::no_id) SetItemTooltip("%s, bits %d..%d", model::to_string(n.dir), n.offset, n.offset + n.width - 1);
            if (indent > 0) Unindent(indent);
            PopID();
        }
        EndCombo();
    }
    if (edited) value = model::join_fields(paths);
    return edited;
}

}

bool edit_category(const model::library& lib, std::string& category)
{
    using namespace ImGui;
    auto& style = GetStyle();
    float button = GetFrameHeight();
    SetNextItemWidth(CalcItemWidth() - button - style.ItemInnerSpacing.x);
    bool changed = input_string("##category", category);
    SetItemTooltip("groups cells in the library panel and the component bar, empty for none");
    SameLine(0, style.ItemInnerSpacing.x);
    if (BeginCombo("##categories", nullptr, ImGuiComboFlags_NoPreview)) {
        if (Selectable("(none)", category.empty())) {
            changed = !category.empty();
            category.clear();
        }
        for (auto& c : lib.categories()) {
            if (Selectable(c.c_str(), c == category)) {
                changed = changed || c != category;
                category = c;
            }
        }
        EndCombo();
    }
    SameLine(0, style.ItemInnerSpacing.x);
    TextUnformatted("Category");
    return changed;
}

bool edit_expr(const char* label, std::string& text, const model::param_values& scope, const char* hint, const std::string& tooltip)
{
    using namespace ImGui;
    std::string error;
    if (!text.empty()) model::eval_expr(text, scope, error);
    if (!error.empty()) PushStyleColor(ImGuiCol_Text, ui::color(ui::color_id::warning));
    bool edited = input_string(label, text, 0, hint);
    if (!error.empty()) PopStyleColor();
    if (!error.empty())
        SetItemTooltip("%s", error.c_str());
    else if (!tooltip.empty())
        SetItemTooltip("%s", tooltip.c_str());
    return edited;
}

bool edit_int_params(std::vector<model::param_def>& params, const char* none)
{
    using namespace ImGui;

    if (params.empty()) {
        PushTextWrapPos(0.0f);
        TextDisabled("%s", none);
        PopTextWrapPos();
    }

    bool edited = false;
    int remove = -1, move_up = -1;
    auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!params.empty() && BeginTable("params", 6, flags)) {
        TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        // two arrows and the remove button
        auto& style = GetStyle();
        float actions = 2 * GetFrameHeight() + CalcTextSize("x").x + 2 * style.FramePadding.x + 4;
        TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, actions);
        TableHeadersRow();

        for (int k = 0; k < (int)params.size(); k++) {
            auto& d = params[k];
            PushID(k);
            TableNextRow();

            TableNextColumn();
            SetNextItemWidth(-FLT_MIN);
            if (input_string("##name", d.name)) {
                d.label = d.name;
                edited = true;
            }
            if (!model::valid_param_name(d.name))
                SetItemTooltip("start with a letter or '_', then letters, digits and '_'");
            else
                SetItemTooltip("values and expressions refer to it by this name, renaming breaks them");

            auto value = [&](const char* id, int64_t& v) {
                TableNextColumn();
                SetNextItemWidth(-FLT_MIN);
                if (InputScalar(id, ImGuiDataType_S64, &v)) edited = true;
            };
            auto def = std::get<int64_t>(d.def);
            value("##default", def);
            value("##min", d.min);
            value("##max", d.max);
            d.def = std::clamp(def, std::min(d.min, d.max), std::max(d.min, d.max));

            TableNextColumn();
            SetNextItemWidth(-FLT_MIN);
            if (input_string("##description", d.description)) edited = true;

            TableNextColumn();
            BeginDisabled(k == 0);
            if (ArrowButton("##up", ImGuiDir_Up)) move_up = k;
            EndDisabled();
            SameLine(0, 2);
            BeginDisabled(k + 1 == (int)params.size());
            if (ArrowButton("##down", ImGuiDir_Down)) move_up = k + 1;
            EndDisabled();
            SameLine(0, 2);
            if (SmallButton("x")) remove = k;
            SetItemTooltip("remove the parameter");

            PopID();
        }
        EndTable();
    }

    if (move_up > 0) {
        std::swap(params[move_up - 1], params[move_up]);
        edited = true;
    }
    if (remove >= 0) {
        params.erase(params.begin() + remove);
        edited = true;
    }
    if (Button("Add parameter")) {
        std::string name;
        for (int n = 1;; n++) {
            name = n == 1 ? "W" : "W" + std::to_string(n);
            if (std::none_of(params.begin(), params.end(), [&](auto& d) { return d.name == name; })) break;
        }
        params.push_back(model::int_param(name, name, 8, 0, model::max_width));
        edited = true;
    }
    return edited;
}

bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base, const model::workspace* ws,
    std::map<std::string, std::string>* exprs, const model::param_values* scope)
{
    using namespace ImGui;

    auto resolved = model::resolve_params(defs, values);
    bool changed = false;

    for (auto& d : defs) {
        PushID(d.name.c_str());

        auto v = resolved.at(d.name);
        bool edited = false;

        switch (d.type) {
        case model::param_def::integer: {
            if (exprs) {
                // a number or an expression of the parameters in scope
                auto it = exprs->find(d.name);
                auto text = it != exprs->end() ? it->second : std::to_string(std::get<int64_t>(v));
                static const model::param_values none;
                auto tip = d.description.empty() ? std::string() : d.description + "\n";
                tip += "a number or an expression of the cell's parameters";
                if (edit_expr(d.label.c_str(), text, scope ? *scope : none, nullptr, tip)) {
                    std::string error;
                    auto number = model::is_literal(text) ? model::eval_expr(text, {}, error) : std::nullopt;
                    if (number) {
                        exprs->erase(d.name);
                        v = std::clamp(*number, d.min, d.max);
                        edited = true;
                    } else if (text.empty()) {
                        // back to the value
                        exprs->erase(d.name);
                        changed = true;
                    } else {
                        (*exprs)[d.name] = text;
                        changed = true;
                    }
                }
                break;
            }
            auto x = std::get<int64_t>(v);
            int64_t step = 1;
            if (InputScalar(d.label.c_str(), ImGuiDataType_S64, &x, &step)) {
                v = std::clamp(x, d.min, d.max);
                edited = true;
            }
        } break;

        case model::param_def::boolean: {
            auto b = std::get<bool>(v);
            if (Checkbox(d.label.c_str(), &b)) {
                v = b;
                edited = true;
            }
        } break;

        case model::param_def::choice: {
            auto& current = std::get<std::string>(v);
            if (BeginCombo(d.label.c_str(), current.c_str())) {
                for (auto& c : d.choices) {
                    if (Selectable(c.c_str(), c == current)) {
                        v = c;
                        edited = true;
                    }
                }
                EndCombo();
            }
        } break;

        case model::param_def::text: {
            auto s = std::get<std::string>(v);
            if (d.file) {
                // the path, "..." and the label
                auto& style = GetStyle();
                float button = CalcTextSize("...").x + style.FramePadding.x * 2.0f;
                SetNextItemWidth(CalcItemWidth() - button - style.ItemInnerSpacing.x);
                edited = input_string("##path", s);
                SameLine(0, style.ItemInnerSpacing.x);
                edited |= browse_button("browse", s, base, firmware_files);
                SameLine(0, style.ItemInnerSpacing.x);
                TextUnformatted(d.label.c_str());
            } else if (ws && d.role == model::param_def::bundle) {
                edited = edit_bundle(d.label.c_str(), s, *ws);
            } else if (ws && d.role == model::param_def::bundle_args && resolved.count(d.bundle_param)) {
                // only for a bundle with parameters
                auto t = ws->find_bundle(model::parse_bundle_ref(std::get<std::string>(resolved.at(d.bundle_param))));
                if (t && !t->params.empty()) {
                    SeparatorText(d.label.c_str());
                    auto args = model::parse_bundle_args(s);
                    if (edit_bundle_args(*t, args)) {
                        s = model::format_bundle_args(ws->bundle_args(t->id, args));
                        edited = true;
                    }
                    Separator();
                }
            } else if (ws && (d.role == model::param_def::fields || d.role == model::param_def::field) && resolved.count(d.bundle_param)) {
                auto bundle = model::parse_bundle_ref(std::get<std::string>(resolved.at(d.bundle_param)));
                auto flipped = resolved.find("flipped");
                // the values of the bundle's parameters, if a parameter has them
                model::param_values args;
                for (auto& a : defs)
                    if (a.role == model::param_def::bundle_args && a.bundle_param == d.bundle_param)
                        args = model::parse_bundle_args(std::get<std::string>(resolved.at(a.name)));
                auto layout = ws->layout_of(bundle, flipped != resolved.end() && std::holds_alternative<bool>(flipped->second) && std::get<bool>(flipped->second),
                    ws->bundle_values(bundle, args));
                if (layout)
                    edited = edit_fields(d.label.c_str(), s, layout, d.role == model::param_def::field);
                else
                    edited = input_string(d.label.c_str(), s);
            } else {
                edited = input_string(d.label.c_str(), s);
            }
            if (edited) v = s;
        } break;
        }

        if (!d.description.empty() && !(exprs && d.type == model::param_def::integer)) SetItemTooltip("%s", d.description.c_str());

        if (edited) {
            model::set_param(values, d, v);
            changed = true;
        }

        PopID();
    }

    return changed;
}

}
