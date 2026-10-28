#include "bundle_ui.h"
#include "../render/renderer.h"
#include "../ui/theme.h"

#include <imgui.h>

#include <algorithm>

namespace logic::editor {

namespace {

const char* bits_text(const model::bundle_node& n, char* buf, size_t size)
{
    if (n.width == 1)
        std::snprintf(buf, size, "%d", n.offset);
    else
        std::snprintf(buf, size, "%d:%d", n.offset + n.width - 1, n.offset);
    return buf;
}

void value_text(const sim::value& v)
{
    auto c = render::value_color(v);
    auto str = sim::to_string(v);
    if (v.width() > 1 && v.width() <= 64 && v.fully_known()) str += " (" + std::to_string(v.to_u64()) + ")";
    ImGui::TextColored({c.r, c.g, c.b, c.a}, "%s", str.c_str());
}

// the name of the field, indented by its depth
void name_cell(const model::bundle_node& n)
{
    using namespace ImGui;
    float indent = n.depth * GetStyle().IndentSpacing * 0.6f;
    if (indent > 0) Indent(indent);
    if (n.bundle != model::no_id)
        TextDisabled("%s", n.name.c_str());
    else
        TextUnformatted(n.name.c_str());
    if (indent > 0) Unindent(indent);
}

}

void bundle_fields_table(const model::bundle_layout& layout, const sim::value* v)
{
    using namespace ImGui;

    if (!layout) {
        TextColored(ui::color(ui::color_id::warning), "%s", layout.error.c_str());
        return;
    }
    if (layout.nodes.empty()) {
        TextDisabled("no fields");
        return;
    }
    if (!BeginTable("fields", v ? 4 : 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) return;
    TableSetupColumn("Field");
    TableSetupColumn("Dir");
    TableSetupColumn("Bits");
    if (v) TableSetupColumn("Value");
    TableHeadersRow();
    for (auto& n : layout.nodes) {
        TableNextRow();
        TableNextColumn();
        name_cell(n);
        TableNextColumn();
        TextDisabled("%s", n.bundle != model::no_id ? (n.flipped ? "~" : "") : model::to_string(n.dir));
        TableNextColumn();
        char buf[32];
        TextDisabled("%s", bits_text(n, buf, sizeof(buf)));
        if (v && n.bundle == model::no_id) {
            TableNextColumn();
            value_text(v->slice(n.offset, n.width));
        }
    }
    EndTable();
}

bool edit_bundle_value(const model::bundle_layout& layout, sim::value& v, const sim::value& current)
{
    using namespace ImGui;

    if (!layout || layout.nodes.empty()) {
        bundle_fields_table(layout);
        return false;
    }
    if (!BeginTable("bundle", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) return false;
    TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed);
    TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    bool changed = false;
    for (auto& n : layout.nodes) {
        PushID(n.path.c_str());
        TableNextRow();
        TableNextColumn();
        name_cell(n);
        if (n.bundle == model::no_id) SetItemTooltip("%s, %d bit%s", model::to_string(n.dir), n.width, n.width == 1 ? "" : "s");
        TableNextColumn();

        if (n.bundle != model::no_id) {
            PopID();
            continue;
        }

        // outputs of the view are driven by the circuit
        if (n.dir == model::direction::output) {
            value_text(current.slice(n.offset, n.width));
            PopID();
            continue;
        }

        auto fv = v.slice(n.offset, n.width);
        sim::value nv = fv;
        bool driven = !fv.fully_floating();
        if (n.dir == model::direction::inout) {
            if (Checkbox("##drive", &driven)) nv = driven ? sim::known(0, n.width) : sim::floating(n.width);
            SetItemTooltip("drive the field, otherwise it's z");
            SameLine();
        }
        BeginDisabled(!driven);
        SetNextItemWidth(-FLT_MIN);
        if (n.width == 1) {
            bool b = fv.get(0) == sim::bit_state::one;
            if (Checkbox("##bit", &b)) nv = sim::known(b, 1);
        } else if (n.width <= 64) {
            uint64_t u = fv.to_u64();
            if (InputScalar("##hex", ImGuiDataType_U64, &u, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal)) nv = sim::known(u, n.width);
            SetItemTooltip("hex, %d bits", n.width);
        } else {
            std::string buf = driven ? sim::to_hex(fv) : std::string();
            buf.resize(model::max_width / 4 + 8);
            if (InputText("##hex", buf.data(), buf.size(), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (auto parsed = sim::parse_hex(buf.c_str(), n.width)) nv = *parsed;
            }
            SetItemTooltip("hex, %d bits, enter sets it", n.width);
        }
        EndDisabled();

        if (!(nv == fv)) {
            v.insert(n.offset, nv);
            changed = true;
        }
        PopID();
    }
    EndTable();
    return changed;
}

bool edit_bundle_args(const model::bundle_type& t, model::param_values& args)
{
    using namespace ImGui;
    bool changed = false;
    for (auto& d : t.params) {
        PushID(d.name.c_str());
        auto it = args.find(d.name);
        int64_t v = it != args.end() && std::holds_alternative<int64_t>(it->second) ? std::get<int64_t>(it->second) : std::get<int64_t>(d.def);
        int64_t step = 1;
        if (InputScalar(d.name.c_str(), ImGuiDataType_S64, &v, &step)) {
            model::set_param(args, d, std::clamp(v, d.min, d.max));
            changed = true;
        }
        auto tip = d.description.empty() ? std::string() : d.description + "\n";
        tip += "default " + std::to_string(std::get<int64_t>(d.def)) + ", " + std::to_string(d.min) + ".." + std::to_string(d.max);
        SetItemTooltip("%s", tip.c_str());
        PopID();
    }
    return changed;
}

}
