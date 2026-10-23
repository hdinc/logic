#include "param_editor.h"
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

bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base, const model::workspace* ws)
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
            } else if (ws && (d.role == model::param_def::fields || d.role == model::param_def::field) && resolved.count(d.bundle_param)) {
                auto bundle = model::parse_bundle_ref(std::get<std::string>(resolved.at(d.bundle_param)));
                auto flipped = resolved.find("flipped");
                auto layout = ws->layout_of(bundle, flipped != resolved.end() && std::holds_alternative<bool>(flipped->second) && std::get<bool>(flipped->second));
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

        if (!d.description.empty()) SetItemTooltip("%s", d.description.c_str());

        if (edited) {
            model::set_param(values, d, v);
            changed = true;
        }

        PopID();
    }

    return changed;
}

}
