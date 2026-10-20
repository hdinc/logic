#include "param_editor.h"
#include "context.h"
#include "file_dialog.h"

#include <algorithm>

namespace logic::editor {

bool edit_params(const std::vector<model::param_def>& defs, model::param_values& values, const std::filesystem::path& base)
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
