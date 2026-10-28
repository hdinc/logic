#include "bundle_editor.h"
#include "../ui/theme.h"
#include "../model/expr.h"
#include "bundle_ui.h"
#include "context.h"
#include "param_editor.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace logic::editor {

namespace {

constexpr size_t undo_limit = 200;

// field names end up in paths ("aw.valid") and hdl signal names ("aw_valid")
bool valid_field_name(const std::string& n)
{
    return !n.empty() && std::all_of(n.begin(), n.end(), [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_'; });
}

}

bundle_editor::bundle_editor(model::workspace& w, model::library& l, model::bundle_type& t)
    : ws(w)
    , lib(l)
    , b(t)
    , current(t)
{
}

void bundle_editor::changed()
{
    lib.dirty = true;
    ws.bundles_changed();
}

bool bundle_editor::unchanged() const
{
    // renaming isn't an undo step
    return b.fields == current.fields && b.params == current.params && b.description == current.description;
}

void bundle_editor::commit()
{
    if (unchanged()) return;
    undo_stack.push_back(std::move(current));
    if (undo_stack.size() > undo_limit) undo_stack.pop_front();
    redo_stack.clear();
    current = b;
}

void bundle_editor::restore()
{
    // the name is changed in the library panel and isn't undone
    auto name = b.name;
    b = current;
    b.name = name;
    current.name = name;
    changed();
}

void bundle_editor::undo()
{
    commit();
    if (undo_stack.empty()) return;
    redo_stack.push_back(std::move(current));
    current = std::move(undo_stack.back());
    undo_stack.pop_back();
    restore();
}

void bundle_editor::redo()
{
    if (redo_stack.empty()) return;
    undo_stack.push_back(std::move(current));
    current = std::move(redo_stack.back());
    redo_stack.pop_back();
    restore();
}

std::vector<std::string> bundle_editor::check() const
{
    std::vector<std::string> r;
    std::set<std::string> params;
    for (auto& d : b.params) {
        if (!model::valid_param_name(d.name))
            r.push_back("parameter '" + d.name + "': start with a letter or '_', then letters, digits and '_'");
        else if (!params.insert(d.name).second)
            r.push_back("duplicate parameter name '" + d.name + "'");
        if (d.min > d.max) r.push_back("parameter '" + d.name + "': the minimum is above the maximum");
    }
    std::set<std::string> names;
    for (auto& f : b.fields) {
        if (f.name.empty())
            r.push_back("a field has no name");
        else if (!valid_field_name(f.name))
            r.push_back("field '" + f.name + "': use only letters, digits and '_'");
        if (!f.name.empty() && !names.insert(f.name).second) r.push_back("duplicate field name '" + f.name + "'");
        if (f.bundle != model::no_id && !ws.find_bundle(f.bundle)) r.push_back("field '" + f.name + "': its bundle type is missing");
    }
    if (auto l = ws.layout_of(b.id); !l) r.push_back(l.error);
    return r;
}

bool bundle_editor::params_table()
{
    ImGui::SeparatorText("Parameters");
    return edit_int_params(b.params, "none, every use of the bundle has the same fields and widths");
}

bool bundle_editor::nested_args(model::bundle_field& f)
{
    using namespace ImGui;

    auto t = ws.find_bundle(f.bundle);
    if (!t) return false;
    bool edited = false;
    auto scope = ws.bundle_values(b.id, {});
    for (auto& d : t->params) {
        PushID(d.name.c_str());
        auto it = f.args.find(d.name);
        std::string text = it != f.args.end() ? it->second : std::string();
        AlignTextToFramePadding();
        TextUnformatted(d.name.c_str());
        SameLine();
        SetNextItemWidth(-FLT_MIN);
        auto hint = std::to_string(std::get<int64_t>(d.def));
        auto tip = "the value of " + d.name + " of " + t->name + ", an expression of this bundle's parameters. empty for its default";
        if (edit_expr("##arg", text, scope, hint.c_str(), tip)) {
            if (text.empty())
                f.args.erase(d.name);
            else
                f.args[d.name] = text;
            edited = true;
        }
        PopID();
    }
    // values of parameters it no longer has
    std::erase_if(f.args, [&](auto& a) {
        bool gone = std::none_of(t->params.begin(), t->params.end(), [&](auto& d) { return d.name == a.first; });
        edited |= gone;
        return gone;
    });
    return edited;
}

void bundle_editor::gui(bool locked)
{
    using namespace ImGui;

    // the name of the library panel's tab, fields are edited here
    Text("Bundle %s / %s", lib.name.c_str(), b.name.c_str());
    auto layout = ws.layout_of(b.id);
    SameLine();
    TextDisabled(layout.width == 1 ? "%d bit" : "%d bits", layout.width);
    if (!b.params.empty()) {
        SameLine();
        TextDisabled("with the default values");
    }

    if (locked) TextColored(ui::color(ui::color_id::warning), "A simulation is running, stop it to edit the bundle");
    BeginDisabled(locked || lib.read_only);

    bool edited = false;
    TextUnformatted("Description");
    if (input_multiline("##description", b.description, {-FLT_MIN, GetTextLineHeight() * 2.5f})) edited = true;

    if (params_table()) edited = true;

    SeparatorText("Fields");
    TextDisabled("Directions are those of the normal view, the flipped view swaps inputs and outputs.");
    if (!b.params.empty()) TextDisabled("Widths are expressions of the parameters, e.g. DATA_W / 8. A field of width 0 is left out.");

    int remove = -1, move_up = -1;
    auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (BeginTable("fields", 5, flags)) {
        TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        TableSetupColumn("Width / view", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        // two arrows and the remove button
        auto& style = GetStyle();
        float actions = 2 * GetFrameHeight() + CalcTextSize("x").x + 2 * style.FramePadding.x + 4;
        TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, actions);
        TableHeadersRow();

        for (int k = 0; k < (int)b.fields.size(); k++) {
            auto& f = b.fields[k];
            PushID(k);
            TableNextRow();

            TableNextColumn();
            SetNextItemWidth(-FLT_MIN);
            if (input_string("##name", f.name)) edited = true;

            // bits with a direction, or a nested bundle
            TableNextColumn();
            SetNextItemWidth(-FLT_MIN);
            auto type = f.bundle != model::no_id ? ws.bundle_label(f.bundle) : std::string(model::to_string(f.dir));
            if (BeginCombo("##type", type.c_str())) {
                for (auto d : {model::direction::input, model::direction::output, model::direction::inout}) {
                    if (Selectable(model::to_string(d), f.bundle == model::no_id && f.dir == d)) {
                        f.bundle = model::no_id;
                        f.flipped = false;
                        f.args.clear();
                        f.dir = d;
                        edited = true;
                    }
                }
                for (auto& l : ws.libraries) {
                    if (l->bundles.empty()) continue;
                    SeparatorText(l->name.c_str());
                    for (auto& o : l->bundles) {
                        // a bundle can't contain itself
                        if (ws.would_create_bundle_cycle(b.id, o->id)) continue;
                        PushID(o.get());
                        if (Selectable(o->name.c_str(), f.bundle == o->id) && f.bundle != o->id) {
                            f.bundle = o->id;
                            f.args.clear();
                            edited = true;
                        }
                        PopID();
                    }
                }
                EndCombo();
            }
            SetItemTooltip("a direction for bits, or a bundle type for a nested bundle");

            TableNextColumn();
            if (f.bundle != model::no_id) {
                if (Checkbox("flipped", &f.flipped)) edited = true;
                SetItemTooltip("the other view of the nested bundle");
                if (nested_args(f)) edited = true;
            } else {
                // a width out of range is a problem too
                std::string error;
                auto w = model::eval_expr(f.width, ws.bundle_values(b.id, {}), error);
                std::string tip;
                if (w && (*w < 0 || *w > model::max_width))
                    tip = "the width must be 0.." + std::to_string(model::max_width);
                else if (w && !model::is_literal(f.width))
                    tip = std::to_string(*w) + " bits with the default values";
                else if (!b.params.empty())
                    tip = "a number or an expression of the parameters, 0 leaves the field out";
                SetNextItemWidth(-FLT_MIN);
                if (edit_expr("##width", f.width, ws.bundle_values(b.id, {}), nullptr, tip)) edited = true;
            }

            TableNextColumn();
            SetNextItemWidth(-FLT_MIN);
            if (input_string("##description", f.description)) edited = true;

            TableNextColumn();
            BeginDisabled(k == 0);
            if (ArrowButton("##up", ImGuiDir_Up)) move_up = k;
            EndDisabled();
            SameLine(0, 2);
            BeginDisabled(k + 1 == (int)b.fields.size());
            if (ArrowButton("##down", ImGuiDir_Down)) move_up = k + 1;
            EndDisabled();
            SameLine(0, 2);
            if (SmallButton(ui::icon::xmark)) remove = k;
            SetItemTooltip("remove the field");

            PopID();
        }
        EndTable();
    }

    if (move_up > 0) {
        std::swap(b.fields[move_up - 1], b.fields[move_up]);
        edited = true;
    }
    if (remove >= 0) {
        b.fields.erase(b.fields.begin() + remove);
        edited = true;
    }
    if (Button((std::string(ui::icon::plus) + " Add field").c_str())) {
        model::bundle_field f;
        for (int n = 1;; n++) {
            f.name = "f" + std::to_string(n);
            if (!b.find_field(f.name)) break;
        }
        b.fields.push_back(f);
        edited = true;
    }
    EndDisabled();

    for (auto& p : check())
        TextColored(ui::color(ui::color_id::warning), "%s", p.c_str());

    SeparatorText("Layout");
    bundle_fields_table(layout);

    auto users = ws.bundle_users(b.id);
    SeparatorText("Used by");
    if (users.empty()) TextDisabled("nothing yet");
    for (auto& u : users)
        BulletText("%s", u.c_str());

    if (edited) changed();
    // one undo step per finished edit, not per key
    if (!IsAnyItemActive()) commit();
}

}
