#include "model_editor.h"
#include "param_editor.h"
#include "file_dialog.h"

#include <algorithm>
#include <filesystem>

namespace logic::editor {

namespace {

// editable list of strings, e.g. source files. a list of paths marks the
// ones that don't exist in dir. with filters every item can be chosen with a
// file dialog.
bool string_list(const char* id, std::vector<std::string>& list, const char* add_label, const std::filesystem::path* dir = nullptr,
    const char* filters = nullptr)
{
    using namespace ImGui;

    bool changed = false;
    PushID(id);
    for (int k = 0; k < (int)list.size(); k++) {
        PushID(k);
        SetNextItemWidth(filters ? -90 : -60);
        if (input_string("##item", list[k])) changed = true;
        if (filters) {
            SameLine();
            if (browse_button("browse", list[k], dir ? *dir : std::filesystem::path{}, filters)) changed = true;
        }
        SameLine();
        if (SmallButton("x")) {
            list.erase(list.begin() + k);
            changed = true;
            PopID();
            break;
        }
        SetItemTooltip("remove");
        if (dir && !dir->empty() && !list[k].empty() && !std::filesystem::exists(*dir / list[k])) {
            SameLine();
            TextColored({1, 0.6f, 0.2f, 1}, "?");
            SetItemTooltip("not found in %s", dir->string().c_str());
        }
        PopID();
    }
    if (SmallButton(add_label)) {
        list.emplace_back();
        changed = true;
    }
    PopID();
    return changed;
}

const char* param_value_text(const model::param_value& v, std::string& buf)
{
    if (auto i = std::get_if<int64_t>(&v)) buf = std::to_string(*i);
    else if (auto b = std::get_if<bool>(&v)) buf = *b ? "true" : "false";
    else buf = std::get<std::string>(v);
    return buf.c_str();
}

}

model_editor::model_editor(context c)
    : ctx(c)
{
}

void model_editor::gui(bool locked)
{
    using namespace ImGui;

    BeginChild("model", {0, 0}, ImGuiChildFlags_None);
    auto& c = ctx.cell;

    // a model in its plugin's library: only the symbol can be changed
    if (!ctx.lib.symbols_dir.empty()) {
        auto m = std::get_if<model::c_model_impl>(&c.impl);
        SeparatorText("Plugin model");
        Text("%s from the plugin %s", m ? m->model.c_str() : "?", m ? m->plugin.c_str() : "?");
        TextDisabled("in %s", ctx.lib.dir.string().c_str());
        TextWrapped("The ports and parameters come from the plugin, File > Reload plugins reads them again. "
                    "The symbol can be edited (Symbol above), it's saved to %s.",
            (ctx.lib.symbols_dir / (c.name + ".json")).string().c_str());
        plugin_tables();
        EndChild();
        return;
    }

    BeginDisabled(locked);
    if (edit_category(ctx.lib, c.category)) ctx.modified();
    if (auto m = std::get_if<model::c_model_impl>(&c.impl)) c_model_settings(*m);
    if (auto h = std::get_if<model::hdl_impl>(&c.impl)) {
        hdl_settings(*h);
        hdl_build(*h);
    }

    Spacing();
    if (Button("Refresh interface")) {
        std::string error;
        refresh_failed = !ctx.ws.refresh_interface(c, error);
        refresh_message = refresh_failed ? error : "ports and parameters updated";
        ctx.modified();
    }
    SetItemTooltip("Read the ports and parameters from the model");
    if (!refresh_message.empty()) {
        SameLine();
        if (refresh_failed)
            TextColored({1, 0.4f, 0.4f, 1}, "%s", refresh_message.c_str());
        else
            TextDisabled("%s", refresh_message.c_str());
    }

    ports_table();
    groups_table();
    params_table();

    EndDisabled();
    EndChild();
}

void model_editor::plugin_tables()
{
    using namespace ImGui;

    auto& c = ctx.cell;
    SeparatorText("Ports");
    if (BeginTable("ports", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        TableSetupColumn("Name");
        TableSetupColumn("Direction");
        TableSetupColumn("Width");
        TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        TableHeadersRow();
        for (auto& p : c.ports) {
            TableNextRow();
            TableNextColumn();
            TextUnformatted(p.name.c_str());
            TableNextColumn();
            TextUnformatted(p.bundle != model::no_id ? "bundle" : model::to_string(p.dir));
            TableNextColumn();
            if (p.bundle != model::no_id)
                Text("%s (%d)", ctx.ws.bundle_label(p.bundle, p.flipped).c_str(), p.width);
            else
                Text("%d", p.width);
            TableNextColumn();
            TextWrapped("%s", p.description.c_str());
        }
        EndTable();
    }

    auto params = c.external_params();
    if (!params || params->empty()) return;
    SeparatorText("Parameters");
    if (BeginTable("params", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        TableSetupColumn("Name");
        TableSetupColumn("Default");
        TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        TableHeadersRow();
        std::string buf;
        for (auto& d : *params) {
            TableNextRow();
            TableNextColumn();
            TextUnformatted(d.label.empty() ? d.name.c_str() : d.label.c_str());
            TableNextColumn();
            TextUnformatted(param_value_text(d.def, buf));
            TableNextColumn();
            TextWrapped("%s", d.description.c_str());
        }
        EndTable();
    }
}

void model_editor::c_model_settings(model::c_model_impl& m)
{
    using namespace ImGui;

    SeparatorText("C model");

    if (ctx.plugins) {
        // the plugins in the plugin folders
        if (BeginCombo("##plugins", "Choose", ImGuiComboFlags_NoPreview)) {
            auto found = ctx.plugins->scan();
            if (found.empty()) TextDisabled("no plugins found");
            for (auto& p : found) {
                if (Selectable(p.name.c_str(), p.name == m.plugin)) {
                    m.plugin = p.name;
                    ctx.modified();
                }
                SetItemTooltip("%s", p.path.string().c_str());
            }
            EndCombo();
        }
        if (IsItemHovered()) {
            std::string dirs;
            for (auto& d : ctx.plugins->search_dirs)
                dirs += "\n" + d.string();
            SetTooltip("plugins found in:%s", dirs.c_str());
        }
        SameLine(0, 2);
    }
    if (browse_button("plugin_file", m.plugin, ctx.lib.dir, plugin_files, "Choose a plugin")) ctx.modified();
    SameLine(0, 2);
    if (input_string("Plugin", m.plugin)) ctx.modified();
    SetItemTooltip("file name of the plugin without extension, found in the plugin folders, or a path relative to the library folder");

    // the models of the plugin
    std::string error;
    auto loaded = ctx.plugins && !m.plugin.empty() ? ctx.plugins->load(m.plugin, ctx.lib.dir, error) : nullptr;
    if (loaded) {
        if (BeginCombo("##models", "Choose", ImGuiComboFlags_NoPreview)) {
            for (uint32_t k = 0; k < loaded->info->model_count; k++) {
                auto& d = loaded->info->models[k];
                if (Selectable(d.name, m.model == d.name)) {
                    m.model = d.name;
                    ctx.modified();
                }
                if (d.description) SetItemTooltip("%s", d.description);
            }
            EndCombo();
        }
        SameLine(0, 2);
    }
    if (input_string("Model", m.model)) ctx.modified();
    SetItemTooltip("name of the model inside the plugin");

    if (!error.empty()) {
        TextColored({1, 0.4f, 0.4f, 1}, "%s", error.c_str());
    } else if (loaded) {
        if (auto d = loaded->find(m.model); d && d->description) TextDisabled("%s", d->description);
    }
}

void model_editor::hdl_settings(model::hdl_impl& h)
{
    using namespace ImGui;

    SeparatorText("HDL");

    int lang = (int)h.language;
    if (Combo("Language", &lang, "Verilog\0SystemVerilog\0VHDL\0")) {
        h.language = (model::hdl_language)lang;
        // a backend for the new language
        if (!model::supports(h.backend, h.language))
            h.backend = h.language == model::hdl_language::vhdl ? model::hdl_backend::ghdl : model::hdl_backend::verilator;
        ctx.modified();
    }

    const char* backends[] = {"Verilator", "Icarus Verilog", "GHDL"};
    if (BeginCombo("Simulator", backends[(int)h.backend])) {
        for (int b = 0; b < 3; b++) {
            if (!model::supports((model::hdl_backend)b, h.language)) continue;
            if (Selectable(backends[b], b == (int)h.backend)) {
                h.backend = (model::hdl_backend)b;
                ctx.modified();
            }
        }
        EndCombo();
    }
    SetItemTooltip("Verilator: fast, 0 and 1 only. Icarus: x and z, delays. GHDL: VHDL.");

    if (input_string(h.language == model::hdl_language::vhdl ? "Top entity" : "Top module", h.top)) ctx.modified();

    auto& dir = ctx.lib.dir;
    TextUnformatted("Sources");
    if (!dir.empty()) SetItemTooltip("relative to %s", dir.string().c_str());
    if (string_list("sources", h.sources, "Add source", &dir, hdl_files)) ctx.modified();

    if (h.language != model::hdl_language::vhdl) {
        TextUnformatted("Include folders");
        if (string_list("includes", h.include_dirs, "Add include folder", &dir)) ctx.modified();
        TextUnformatted("Defines");
        SetItemTooltip("NAME or NAME=VALUE");
        if (string_list("defines", h.defines, "Add define")) ctx.modified();
    }

    if (input_string("Extra arguments", h.args)) ctx.modified();
    SetItemTooltip("passed to the tool as they are");
}

void model_editor::hdl_build(const model::hdl_impl& h)
{
    using namespace ImGui;

    if (!ctx.hdl) return;
    SeparatorText("Build");

    bool ok;
    auto tool = ctx.hdl->tool_status(h.backend, ok);
    if (!ok) {
        TextColored({1, 0.4f, 0.4f, 1}, "%s", tool.c_str());
        return;
    }
    TextDisabled("%s", tool.c_str());

    auto st = ctx.hdl->status(ctx.cell.id);
    bool running = st.state == hdl::build_status::queued || st.state == hdl::build_status::building;
    BeginDisabled(running);
    if (Button("Build")) {
        if (auto params = ctx.cell.external_params()) ctx.hdl->build(ctx.cell, model::resolve_params(*params, {}));
    }
    EndDisabled();
    SetItemTooltip("Build (Verilator) or compile (Icarus) the model with the default parameters. Simulating builds what's missing too.");
    SameLine();

    switch (st.state) {
    case hdl::build_status::none: TextDisabled("not built in this session"); break;
    case hdl::build_status::queued: TextColored({0.4f, 0.7f, 1, 1}, "waiting..."); break;
    case hdl::build_status::building: TextColored({0.4f, 0.7f, 1, 1}, "building..."); break;
    case hdl::build_status::done: TextColored({0.4f, 0.9f, 0.4f, 1}, "built"); break;
    case hdl::build_status::failed: TextColored({1, 0.4f, 0.4f, 1}, "failed"); break;
    }

    if (!st.error.empty()) TextWrapped("%s", st.error.c_str());

    // messages with a place in the sources
    for (auto& d : st.diagnostics) {
        auto color = d.severity == "error" ? ImVec4{1, 0.4f, 0.4f, 1} : ImVec4{1, 0.8f, 0.3f, 1};
        auto where = d.file.empty() ? std::string() : std::filesystem::path(d.file).filename().string() + ":" + std::to_string(d.line) + ": ";
        TextColored(color, "%s%s", where.c_str(), d.message.c_str());
        if (!d.file.empty()) SetItemTooltip("%s:%d:%d", d.file.c_str(), d.line, d.column);
    }

    if (!st.log.empty() && TreeNode("Output")) {
        BeginChild("log", {0, 200}, ImGuiChildFlags_Borders);
        for (auto& line : st.log)
            TextUnformatted(line.c_str());
        // follow the end while building
        if (running && GetScrollY() >= GetScrollMaxY() - 20) SetScrollHereY(1.0f);
        EndChild();
        TreePop();
    }
}

void model_editor::ports_table()
{
    using namespace ImGui;

    auto& c = ctx.cell;
    SeparatorText("Ports");
    TextDisabled("Found by refreshing, or entered by hand");

    // with bundle ports the model's own ports are edited, the cell's are
    // made from them
    bool grouped = c.port_groups() && !c.port_groups()->empty();
    auto& list = grouped ? *c.leaf_ports() : c.ports;
    if (grouped) TextDisabled("The model's ports, some of them make up the bundle ports below");
    bool changed = false;

    model::id remove = model::no_id;
    if (BeginTable("ports", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2);
        TableSetupColumn("Direction", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        TableSetupColumn("Width", ImGuiTableColumnFlags_WidthStretch, 1);
        TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 3);
        TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        TableHeadersRow();

        for (auto& p : list) {
            PushID((int)p.id);
            TableNextRow();
            TableNextColumn();
            SetNextItemWidth(-1);
            if (input_string("##name", p.name)) changed = true;
            TableNextColumn();
            SetNextItemWidth(-1);
            int dir = (int)p.dir;
            if (Combo("##dir", &dir, "input\0output\0inout\0")) {
                p.dir = (model::direction)dir;
                changed = true;
            }
            TableNextColumn();
            SetNextItemWidth(-1);
            if (InputInt("##width", &p.width, 0)) {
                p.width = std::clamp(p.width, 1, model::max_width);
                changed = true;
            }
            TableNextColumn();
            SetNextItemWidth(-1);
            if (input_string("##desc", p.description)) changed = true;
            TableNextColumn();
            if (SmallButton("x")) remove = p.id;
            SetItemTooltip("remove the port and its pin");
            PopID();
        }
        EndTable();
    }
    if (remove != model::no_id) {
        if (grouped)
            std::erase_if(list, [&](auto& p) { return p.id == remove; });
        else
            c.remove_port(remove);
        changed = true;
    }

    if (Button("Add port")) {
        if (grouped) {
            model::port p;
            p.id = c.new_id();
            for (int n = 1; p.name.empty(); n++) {
                auto name = "in" + std::to_string(n);
                if (std::none_of(list.begin(), list.end(), [&](auto& o) { return o.name == name; })) p.name = name;
            }
            list.push_back(p);
        } else {
            c.add_port(model::direction::input, {0, 0});
        }
        changed = true;
    }
    if (changed) {
        if (grouped) ctx.ws.apply_groups(c);
        ctx.modified();
    }
    SameLine();
    if (Button("Generate symbol")) {
        c.generate_symbol(model::pin_order::declaration);
        ctx.modified();
    }
    SetItemTooltip("Replace the symbol with a box: inputs left, outputs right, in the order of the ports");

    for (auto& problem : c.check())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", problem.c_str());
}

void model_editor::groups_table()
{
    using namespace ImGui;

    auto& c = ctx.cell;
    auto groups = c.port_groups();
    if (!groups) return;

    SeparatorText("Bundle ports");
    TextDisabled("Ports of the model named <prefix><field> as one bundle port, e.g. m0_valid and m0_data as m0");

    bool changed = false;
    int remove = -1;
    if (!groups->empty() && BeginTable("groups", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        TableSetupColumn("Bundle", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        TableSetupColumn("Flipped", ImGuiTableColumnFlags_WidthFixed);
        TableSetupColumn("Prefix", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        TableSetupColumn("Side", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        TableHeadersRow();
        for (int k = 0; k < (int)groups->size(); k++) {
            auto& g = (*groups)[k];
            PushID(k);
            TableNextRow();
            TableNextColumn();
            SetNextItemWidth(-1);
            if (input_string("##name", g.name)) changed = true;
            TableNextColumn();
            SetNextItemWidth(-1);
            if (BeginCombo("##bundle", ctx.ws.bundle_label(g.bundle).c_str())) {
                for (auto& l : ctx.ws.libraries) {
                    if (l->bundles.empty()) continue;
                    SeparatorText(l->name.c_str());
                    for (auto& b : l->bundles) {
                        PushID(b.get());
                        if (Selectable(b->name.c_str(), g.bundle == b->id)) {
                            g.bundle = b->id;
                            changed = true;
                        }
                        PopID();
                    }
                }
                EndCombo();
            }
            TableNextColumn();
            if (Checkbox("##flipped", &g.flipped)) changed = true;
            SetItemTooltip("the model is the other side of the bundle: its inputs and outputs swapped");
            TableNextColumn();
            SetNextItemWidth(-1);
            if (input_string("##prefix", g.prefix)) changed = true;
            SetItemTooltip("the model's ports of the fields start with this, empty for %s_", g.name.c_str());
            TableNextColumn();
            SetNextItemWidth(-1);
            int side = g.side == model::direction::input ? 0 : 1;
            if (Combo("##side", &side, "left\0right\0")) {
                g.side = side == 0 ? model::direction::input : model::direction::output;
                changed = true;
            }
            SetItemTooltip("the side of the generated symbol");
            TableNextColumn();
            if (SmallButton("x")) remove = k;
            SetItemTooltip("back to the model's ports");
            PopID();
        }
        EndTable();
    }
    if (remove >= 0) {
        groups->erase(groups->begin() + remove);
        changed = true;
    }

    // prefixes whose ports fit a bundle type, in either view
    if (Button("Find bundle ports")) OpenPopup("find_groups");
    SetItemTooltip("Look for ports of the model that make up a bundle type, or that share a prefix for a new one");
    if (BeginPopup("find_groups")) {
        auto& leaves = c.model_ports();
        bool any = false;
        SeparatorText("Existing bundle types");
        for (auto& l : ctx.ws.libraries) {
            for (auto& b : l->bundles) {
                for (bool flipped : {false, true}) {
                    auto layout = ctx.ws.layout_of(b->id, flipped);
                    if (!layout || layout.leaves.empty()) continue;
                    // ports ending in the first field give the prefixes
                    auto first = layout.leaves[0].path;
                    std::replace(first.begin(), first.end(), '.', '_');
                    for (auto& p : leaves) {
                        if (p.name.size() <= first.size() || p.name.compare(p.name.size() - first.size(), first.size(), first) != 0) continue;
                        model::port_group g;
                        g.prefix = p.name.substr(0, p.name.size() - first.size());
                        g.name = g.prefix.size() > 1 && g.prefix.back() == '_' ? g.prefix.substr(0, g.prefix.size() - 1) : g.prefix;
                        if (g.name.empty()) g.name = b->name;
                        if (g.prefix == g.name + "_") g.prefix.clear();
                        g.bundle = b->id;
                        g.flipped = flipped;
                        g.side = flipped ? model::direction::input : model::direction::output;
                        // all fields there and not in a group yet
                        auto others = *groups;
                        others.push_back(g);
                        if (!ctx.ws.group_ports(leaves, others).problems.empty()) continue;
                        any = true;
                        auto label = g.name + " : " + ctx.ws.bundle_label(b->id, flipped);
                        if (MenuItem(label.c_str())) {
                            groups->push_back(g);
                            changed = true;
                        }
                    }
                }
            }
        }
        if (!any) TextDisabled("none fits the model's ports");

        // new types from ports with a common prefix that aren't grouped yet
        auto grouping = ctx.ws.group_ports(leaves, *groups);
        auto grouped = [&](int k) { return grouping.ports[grouping.model_ports[k].of].bundle != model::no_id; };
        bool header = false;
        for (auto& p : model::common_prefixes(leaves)) {
            std::erase_if(p.ports, grouped);
            if (p.ports.size() < 2) continue;
            if (!header) {
                SeparatorText("New bundle types");
                header = true;
            }
            std::string fields;
            for (int k : p.ports)
                fields += (fields.empty() ? "" : ", ") + leaves[k].name.substr(p.prefix.size());
            auto label = p.prefix.substr(0, p.prefix.size() - 1) + ": " + fields;
            PushID(p.prefix.c_str());
            if (MenuItem(label.c_str())) {
                // the type goes to the cell's library, the ports keep their order
                groups->push_back(ctx.ws.bundle_from_ports(ctx.lib, leaves, p));
                changed = true;
            }
            SetItemTooltip("make a bundle type of these ports and group them into one bundle port");
            PopID();
        }
        EndPopup();
    }
    SameLine();
    if (Button("Add bundle port")) {
        model::port_group g;
        g.name = "bus";
        for (auto& l : ctx.ws.libraries)
            if (!l->bundles.empty() && g.bundle == model::no_id) g.bundle = l->bundles.front()->id;
        groups->push_back(g);
        changed = true;
    }

    if (changed) {
        ctx.ws.apply_groups(c);
        ctx.modified();
    }
    if (!groups->empty())
        for (auto& p : ctx.ws.group_ports(c.model_ports(), *groups).problems)
            TextColored({1, 0.6f, 0.2f, 1}, "%s", p.c_str());
}

void model_editor::params_table()
{
    using namespace ImGui;

    auto params = ctx.cell.external_params();
    if (!params) return;

    SeparatorText("Parameters");
    if (params->empty()) {
        TextDisabled("none, refreshing finds them in the model");
        return;
    }

    std::string buf;
    if (BeginTable("params", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        TableSetupColumn("Name");
        TableSetupColumn("Default");
        TableSetupColumn("Description");
        TableHeadersRow();
        for (auto& d : *params) {
            TableNextRow();
            TableNextColumn();
            TextUnformatted(d.name.c_str());
            TableNextColumn();
            TextUnformatted(param_value_text(d.def, buf));
            TableNextColumn();
            TextUnformatted(d.description.c_str());
        }
        EndTable();
    }
    TextDisabled("Instances set the values in their properties");
}

}
