#include "number_ui.h"
#include "../sim/numbers.h"
#include "../ui/theme.h"

#include <imgui.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace logic::editor {

bool edit_number_format(const char* label, model::number_format& f, int width)
{
    using namespace ImGui;
    bool changed = false;
    if (BeginCombo(label, model::kind_label(f.kind))) {
        for (int k = 0; k < model::number_kind_count; k++) {
            auto kind = (model::number_format::kind_t)k;
            if (!model::fits(kind, width)) continue;
            if (Selectable(model::kind_label(kind), f.kind == kind)) {
                changed = f.kind != kind;
                f.kind = kind;
            }
        }
        EndCombo();
    }
    if (f.kind == model::number_format::unsigned_fixed || f.kind == model::number_format::signed_fixed) {
        int frac = f.frac;
        if (InputInt("Fraction bits", &frac)) {
            frac = std::clamp(frac, 0, std::min(width, 64));
            changed = changed || frac != f.frac;
            f.frac = frac;
        }
        SetItemTooltip("the value is the bits / 2^fraction bits");
    }
    return changed;
}

bool edit_number(const char* label, sim::value& v, int width, model::number_format f)
{
    using namespace ImGui;

    // the text being typed, per widget, while it has the focus. otherwise
    // the current value is shown.
    static std::map<ImGuiID, std::string> typing;
    auto id = GetID(label);
    auto it = typing.find(id);
    std::string text = it != typing.end() ? it->second : sim::format_number(v, f);
    // red only while typing something that isn't a value
    bool valid = it == typing.end() || sim::parse_number(text, width, f).has_value();

    // room for every bit in binary, with separators
    std::vector<char> buf(std::max<size_t>(text.size(), 2 * width) + 64, 0);
    text.copy(buf.data(), buf.size() - 1);
    if (!valid) PushStyleColor(ImGuiCol_Text, ui::color(ui::color_id::error));
    bool enter = InputText(label, buf.data(), buf.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (!valid) PopStyleColor();
    if (IsItemActive())
        typing[id] = buf.data();
    else
        typing.erase(id);
    SetItemTooltip("%s, enter sets it", model::kind_label(f.kind));

    if (!enter) return false;
    typing.erase(id);
    auto parsed = sim::parse_number(buf.data(), width, f);
    if (!parsed || *parsed == v) return false;
    v = *parsed;
    return true;
}

bool edit_bits(sim::value& v)
{
    using namespace ImGui;
    bool changed = false;
    float size = GetFrameHeight() * 0.75f;
    PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 2});
    // rows of 16 bits, the top bits first
    for (int i = v.width() - 1; i >= 0; i--) {
        PushID(i);
        auto b = v.get(i);
        const char* text = b == sim::bit_state::one ? "1" : b == sim::bit_state::zero ? "0"
            : b == sim::bit_state::x                                                  ? "x"
                                                                                      : "z";
        if (b == sim::bit_state::one) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (Button(text, {size, size})) {
            v.set(i, b == sim::bit_state::one ? sim::bit_state::zero : sim::bit_state::one);
            changed = true;
        }
        if (b == sim::bit_state::one) PopStyleColor();
        SetItemTooltip("bit %d", i);
        PopID();
        if (i % 16 != 0)
            SameLine(0, i % 4 == 0 ? 8.0f : 2.0f);
    }
    PopStyleVar(2);
    return changed;
}

}
