#include "sim_view.h"
#include "bundle_ui.h"
#include "../ui/fonts.h"

#include <imgui_internal.h>

#include <cmath>

namespace logic::editor {

namespace {

std::string scope_label(const sim::scope& s)
{
    return s.parent ? s.name + " (" + s.cell->name + ")" : s.cell->name;
}

}

sim_view::sim_view(model::workspace& w, render::renderer& rr, std::shared_ptr<sim_session> session, std::vector<int> p)
    : ws(w)
    , r(rr)
    , s(std::move(session))
    , path(std::move(p))
{
}

const sim::scope& sim_view::current() const
{
    const sim::scope* sc = &s->sim().root();
    for (int i : path) {
        auto c = sc->child(i);
        if (!c) break;
        sc = c;
    }
    return *sc;
}

std::vector<int> sim_view::path_of(const sim::scope& target) const
{
    std::vector<int> p;
    for (auto sc = &target; sc->parent; sc = sc->parent) {
        auto& siblings = sc->parent->child_index;
        for (int i = 0; i < (int)siblings.size(); i++)
            if (sc->parent->child(i) == sc) p.insert(p.begin(), i);
    }
    return p;
}

std::string sim_view::title() const
{
    auto& sc = current();
    if (!sc.parent) return sc.cell->name;
    return s->sim().root().cell->name + "/" + sc.path + " (" + sc.cell->name + ")";
}

schematic_editor& sim_view::editor_of(const model::cell& c)
{
    auto& e = editors[c.id];
    if (!e) {
        // only used for drawing and hit testing, the cell is not changed
        auto& cell = const_cast<model::cell&>(c);
        e = std::make_unique<schematic_editor>(context{ws, *ws.library_of(c.id), cell, r});
    }
    return *e;
}

void sim_view::gui(Hd::Texture& tex, Hd::FrameBuffer& fb)
{
    using namespace ImGui;

    toolbar();
    breadcrumb();

    auto& sc = current();
    auto& e = editor_of(*sc.cell);

    // whole pixels, the framebuffer is shown 1:1 so small text stays sharp
    auto size = GetContentRegionAvail();
    size.x = std::min(std::floor(size.x), (float)tex.GetWidth());
    size.y = std::min(std::floor(size.y), (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};
    e.view.begin_frame({size.x, size.y});
    e.view.set_mouse(ndc);

    e.sim = &s->sim();
    e.sim_scope = &sc;
    if (hovered) e.view.handle_view_input();
    auto path_before = path;
    canvas_inputs(hovered, e);
    // another scope: nothing is selected in it
    if (path != path_before) select(-1);

    // the path may have changed, draw what is shown now
    auto& shown = current();
    auto& de = editor_of(*shown.cell);
    de.view.begin_frame({size.x, size.y});
    de.sim = &s->sim();
    de.sim_scope = &shown;
    // the net under the mouse, with its wires and tunnels
    de.highlight_net = hovered && &de == &e ? de.net_at(de.view.mouse_world()) : -1;

    fb.Bind((int)size.x, (int)size.y);
    de.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, size.y / tex.GetHeight()}, {size.x / tex.GetWidth(), 0});

    instance_menu();

    // a held button is let go anywhere
    if (pressed >= 0 && !IsMouseDown(ImGuiMouseButton_Left)) {
        s->interact(&current(), pressed, "release", 0);
        pressed = -1;
    }
}

void sim_view::toolbar()
{
    using namespace ImGui;

    auto& run = s->run;
    auto& sim = s->sim();

    if (Button("Waveforms")) s->waveforms_visible = true;
    SameLine();
    if (Button("Signals...")) OpenPopup("signals");
    signals_menu();
    SameLine();

    if (Button("Reset")) {
        run.run(false);
        run.reset();
    }
    SetItemTooltip("Back to time 0 and the initial state, inputs keep their values");
    SameLine();

    if (Button("Step")) run.step();
    SetItemTooltip("To the next event, e.g. a clock edge (Space)");
    SameLine();

    bool running = run.running();
    if (running) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (Button(running ? "Pause" : "Run")) run.run(!running);
    if (running) PopStyleColor();
    SetItemTooltip("Let the simulated time pass");
    SameLine();

    // simulated seconds per real second, 0 is as fast as possible
    double speed = run.speed();
    bool max = speed <= 0.0;
    if (Checkbox("Max", &max)) run.set_speed(max ? 0.0 : 1.0);
    SetItemTooltip("As fast as possible instead of a fixed speed");
    if (!max) {
        SameLine();
        float f = (float)speed;
        SetNextItemWidth(140);
        if (DragFloat("##speed", &f, 0.01f, 1e-9f, 1000.0f, "%.3g x", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            run.set_speed(f);
        SetItemTooltip("Simulated time per real time, 1 is real time");
    }
    SameLine();

    Text("t = %s", sim::format_time(sim.now()).c_str());
    if (running) {
        SameLine();
        double actual = run.actual_speed();
        if (!max && actual > 0.0 && actual < speed * 0.9)
            TextColored({1, 0.6f, 0.2f, 1}, "(%.3g x, slower than set)", actual);
        else
            TextDisabled("(%.3g x)", actual);
    }
    if (sim.oscillating()) {
        SameLine();
        TextColored({1, 0.3f, 0.3f, 1}, "oscillating");
    }
    if (!sim.paused().empty() && !running) {
        SameLine();
        TextColored({1, 0.75f, 0.2f, 1}, "paused, %s", sim.paused().c_str());
    }
    if (!sim.problems().empty()) {
        SameLine();
        TextColored({1, 0.6f, 0.2f, 1}, "%zu problem(s), see properties", sim.problems().size());
    }
}

void sim_view::breadcrumb()
{
    using namespace ImGui;

    // top cell first
    std::vector<const sim::scope*> chain;
    for (auto sc = &current(); sc; sc = sc->parent)
        chain.insert(chain.begin(), sc);

    PushStyleVar(ImGuiStyleVar_FramePadding, {4, 1});
    for (size_t k = 0; k < chain.size(); k++) {
        if (k > 0) {
            SameLine();
            TextDisabled(">");
            SameLine();
        }
        PushID((int)k);
        bool last = k + 1 == chain.size();
        if (last) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (Button(scope_label(*chain[k]).c_str())) path.resize(k);
        if (last) PopStyleColor();
        PopID();
    }
    PopStyleVar();

    if (!path.empty()) {
        SameLine();
        TextDisabled("(backspace: up)");
    } else {
        SameLine();
        TextDisabled("(double click an instance to look inside)");
    }
}

void sim_view::canvas_inputs(bool hovered, schematic_editor& e)
{
    using namespace ImGui;

    if (!hovered) return;

    auto& sim = s->sim();
    auto& sc = current();
    auto m = e.view.mouse_world();

    e.hover_tooltip();

    if (IsKeyPressed(ImGuiKey_Space)) s->run.step();
    if (IsKeyPressed(ImGuiKey_Backspace) && !path.empty()) path.pop_back();

    auto instance_at = [&]() -> int {
        auto sch = sc.cell->get_schematic();
        for (auto& h : e.hit_test(m)) {
            if (h.kind != schematic_editor::handle::instance) continue;
            for (int i = 0; i < (int)sch->instances.size(); i++)
                if (sch->instances[i].id == h.id) return i;
        }
        return -1;
    };

    if (IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        int i = instance_at();
        if (i >= 0 && sc.child(i)) {
            path.push_back(i);
            return;
        }
    }

    // select, press buttons and switches
    if (IsMouseClicked(ImGuiMouseButton_Left)) {
        int i = instance_at();
        select(i);
        if (i >= 0) {
            auto cell = ws.find_cell(sc.cell->get_schematic()->instances[i].cell);
            auto prim = cell ? ws.primitive_of(*cell) : nullptr;
            if (prim && (prim->kind() == "button" || prim->kind() == "switch")) {
                s->interact(&sc, i, "press", 0);
                if (prim->kind() == "button") pressed = i;
            }
        }
    }

    // inputs of the top cell are set by the user, inside an instance they
    // come from the parent
    if (!sc.parent && IsMouseClicked(ImGuiMouseButton_Left)) {
        for (auto& h : e.hit_test(m)) {
            if (h.kind != schematic_editor::handle::port) continue;
            for (int k = 0; k < (int)sc.cell->ports.size(); k++) {
                auto& p = sc.cell->ports[k];
                if (p.id != h.id || p.dir != model::direction::input || p.width != 1) continue;
                auto v = sim.input_value(k);
                s->run.set_input(k, sim::known(v.get(0) != sim::bit_state::one, 1));
            }
        }
    }

    // a right click without dragging, a drag pans the view
    if (IsMouseReleased(ImGuiMouseButton_Right)) {
        auto d = GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        int i = instance_at();
        if (d.x * d.x + d.y * d.y < 9.0f) {
            menu_instance = i;
            menu_net = -1;
            float tol = std::max(3.0f, e.view.pixels(6));
            for (int k = 0; k < (int)sc.cell->ports.size(); k++) {
                auto& p = sc.cell->ports[k];
                if (glm::length(m - glm::vec2(p.pos)) <= std::max(6.0f, tol) ||
                    glm::length(m - glm::vec2(model::connection_point(p))) <= tol) {
                    menu_net = sim.net_of_port(sc, k);
                    menu_signal_name = p.name;
                    break;
                }
            }
            auto sch = sc.cell->get_schematic();
            if (menu_net < 0 && sch) {
                for (int j = (int)sch->instances.size() - 1; j >= 0 && menu_net < 0; j--) {
                    auto& inst = sch->instances[j];
                    auto iface = ws.interface_of(inst);
                    if (!iface) continue;
                    for (auto& pin : iface.symbol->pins) {
                        if (glm::length(m - glm::vec2(model::pin_position(inst, pin))) > tol) continue;
                        for (int k = 0; k < (int)iface.ports->size(); k++) {
                            if ((*iface.ports)[k].id != pin.port) continue;
                            menu_net = pin_net(j, k);
                            menu_signal_name = inst.name + "." + (*iface.ports)[k].name;
                        }
                    }
                }
            }
            if (menu_net < 0) {
                auto wire = e.wire_at(m);
                if (wire != model::no_id) {
                    menu_net = sim.net_of_wire(sc, wire);
                    menu_signal_name = "wire " + std::to_string(wire);
                }
            }
            if (menu_net >= 0 || menu_instance >= 0) OpenPopup("instance");
        }
    }
}

int sim_view::pin_net(int instance, int port) const
{
    auto& sc = current();
    auto& nets = sc.nets->instance_nets;
    int local = instance >= 0 && instance < (int)nets.size() && port >= 0 && port < (int)nets[instance].size() ? nets[instance][port] : -1;
    return local >= 0 ? sc.flat_net[local] : -1;
}

void sim_view::record_signal(int net, const std::string& name)
{
    if (net < 0 || s->sim().net_width(net) == 0) return;
    auto& sc = current();
    auto label = s->sim().root().cell->name + "/" + (sc.path.empty() ? "" : sc.path + "/") + name;

    // a bundle is recorded field by field
    std::vector<model::bundle_leaf> fields;
    for (size_t local = 0; local < sc.flat_net.size(); local++) {
        auto& n = sc.nets->nets[local];
        if (sc.flat_net[local] != net || n.bundle == model::no_id || n.type_error || n.width_error) continue;
        auto layout = ws.layout_of(n.bundle);
        if (layout && layout.width == s->sim().net_width(net)) fields = layout.leaves;
        break;
    }

    // only the recorder changes, real time running goes on undisturbed
    s->run.query([net, label, fields](sim::simulator& sim) {
        if (fields.empty()) sim.waveforms.add(sim, net, label);
        for (auto& f : fields)
            sim.waveforms.add(sim, net, label + "." + f.path, f.offset, f.width);
    });
    s->waveforms_visible = true;
}

void sim_view::signals_menu()
{
    using namespace ImGui;
    if (!BeginPopup("signals")) return;
    auto& sc = current();
    auto& recorder = s->run.sim().waveforms;
    size_t recorded = recorder.size();
    TextDisabled("Add signals in %s", scope_label(sc).c_str());
    TextDisabled("%zu / %zu signals (connected aliases share a trace)", recorded, sim::waveform_recorder::max_signals);
    auto item = [&](int net, const std::string& name) {
        bool exists = recorder.contains(net);
        bool enabled = net >= 0 && s->sim().net_width(net) > 0 && !exists && recorded < sim::waveform_recorder::max_signals;
        PushID(net);
        if (MenuItem(name.c_str(), nullptr, exists, enabled)) record_signal(net, name);
        PopID();
    };
    if (BeginMenu("Cell ports")) {
        for (int k = 0; k < (int)sc.cell->ports.size(); k++) item(s->sim().net_of_port(sc, k), sc.cell->ports[k].name);
        EndMenu();
    }
    if (auto sch = sc.cell->get_schematic()) {
        if (BeginMenu("Instance pins")) {
            for (int i = 0; i < (int)sch->instances.size(); i++) {
                auto& inst = sch->instances[i];
                PushID(i);
                if (BeginMenu(inst.name.c_str())) {
                    auto iface = ws.interface_of(inst);
                    if (iface)
                        for (int k = 0; k < (int)iface.ports->size(); k++) item(pin_net(i, k), inst.name + "." + (*iface.ports)[k].name);
                    EndMenu();
                }
                PopID();
            }
            EndMenu();
        }
        if (BeginMenu("Wires")) {
            // Wire segments of the same electrical net need only one entry.
            std::vector<int> seen;
            for (auto& wire : sch->wires) {
                int net = s->sim().net_of_wire(sc, wire.id);
                if (std::find(seen.begin(), seen.end(), net) != seen.end()) continue;
                seen.push_back(net);
                item(net, "wire " + std::to_string(wire.id));
            }
            EndMenu();
        }
    }
    EndPopup();
}

void sim_view::select(int instance)
{
    if (instance == selected) return;
    selected = instance;
    memory = 0;
    memory_address = 0;
    follow_pc = true;
    s->watch(instance >= 0 ? &current() : nullptr, instance);
}

void sim_view::instance_menu()
{
    using namespace ImGui;

    if (!BeginPopup("instance")) return;

    auto& sc = current();
    auto child = menu_instance >= 0 ? sc.child(menu_instance) : nullptr;
    auto& recorder = s->run.sim().waveforms;
    size_t recorded = recorder.size();
    auto add_item = [&](int net, const std::string& name, const char* label) {
        bool exists = recorder.contains(net);
        bool enabled = net >= 0 && s->sim().net_width(net) > 0 && !exists && recorded < sim::waveform_recorder::max_signals;
        if (MenuItem(label, nullptr, exists, enabled)) record_signal(net, name);
    };
    if (recorded >= sim::waveform_recorder::max_signals)
        TextDisabled("Signal limit reached; remove a waveform to add another.");
    if (menu_net >= 0) {
        TextDisabled("%s", menu_signal_name.c_str());
        add_item(menu_net, menu_signal_name, "Add to waveforms");
    }
    if (menu_instance >= 0 && BeginMenu("Add pins to waveforms")) {
        auto& inst = sc.cell->get_schematic()->instances[menu_instance];
        auto iface = ws.interface_of(inst);
        if (iface) {
            for (int k = 0; k < (int)iface.ports->size(); k++) {
                int net = pin_net(menu_instance, k);
                auto& name = (*iface.ports)[k].name;
                add_item(net, inst.name + "." + name, name.c_str());
            }
        }
        EndMenu();
    }
    if (child) {
        TextDisabled("%s", scope_label(*child).c_str());
        if (Selectable("Open")) path.push_back(menu_instance);
        if (Selectable("Open in new tab")) {
            auto p = path;
            p.push_back(menu_instance);
            open_in_new_tab = p;
        }
    }
    EndPopup();
}

void sim_view::property_panel()
{
    using namespace ImGui;

    auto& sim = s->sim();
    auto& sc = current();

    SeparatorText("Simulation");
    Text("Top: %s, t = %s", sim.root().cell->name.c_str(), sim::format_time(sim.now()).c_str());
    if (sim.oscillating()) TextColored({1, 0.3f, 0.3f, 1}, "The circuit doesn't settle, e.g. a gate feeding itself");
    for (auto& p : sim.problems())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", p.c_str());

    auto& ports = sc.cell->ports;

    if (!sc.parent) {
        SeparatorText("Inputs");
        for (int k = 0; k < (int)ports.size(); k++) {
            auto& p = ports[k];
            // bundles have inputs whatever side they're on
            if (p.bundle != model::no_id) {
                auto layout = ws.layout_of(p.bundle, p.flipped);
                if (layout && layout.width == p.width) {
                    PushID(k);
                    if (TreeNodeEx(p.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s : %s", p.name.c_str(), ws.bundle_label(p.bundle, p.flipped).c_str())) {
                        auto v = sim.input_value(k);
                        if (edit_bundle_value(layout, v, sim.port_value(k))) s->run.set_input(k, v);
                        TreePop();
                    }
                    PopID();
                    continue;
                }
            }
            if (p.dir == model::direction::output) continue;
            PushID(k);
            auto v = sim.input_value(k);

            // inouts can let go of their net
            bool driven = !v.fully_floating();
            if (p.dir == model::direction::inout) {
                if (Checkbox("##drive", &driven)) s->run.set_input(k, driven ? sim::known(0, p.width) : sim::floating(p.width));
                SetItemTooltip("drive the inout, otherwise it's z");
                SameLine();
            }
            BeginDisabled(!driven);

            if (p.width == 1) {
                bool b = v.get(0) == sim::bit_state::one;
                if (Checkbox(p.name.c_str(), &b)) s->run.set_input(k, sim::known(b, 1));
            } else if (p.width <= 64) {
                uint64_t u = v.to_u64();
                if (InputScalar(p.name.c_str(), ImGuiDataType_U64, &u, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal))
                    s->run.set_input(k, sim::known(u, p.width));
                SetItemTooltip("hex, %d bits", p.width);
            } else {
                // too wide for a number, edited as hex text
                std::string buf = driven ? sim::to_hex(v) : std::string();
                buf.resize(model::max_width / 4 + 8);
                if (InputText(p.name.c_str(), buf.data(), buf.size(), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (auto parsed = sim::parse_hex(buf.c_str(), p.width)) s->run.set_input(k, *parsed);
                }
                SetItemTooltip("hex, %d bits, enter sets it", p.width);
            }

            EndDisabled();
            PopID();
        }
    } else {
        SeparatorText("Instance");
        Text("%s", title().c_str());
        TextDisabled("ports are driven by the parent");
    }

    instance_panel();

    SeparatorText(sc.parent ? "Ports" : "Outputs");
    if (BeginTable("ports", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (int k = 0; k < (int)ports.size(); k++) {
            auto& p = ports[k];
            // the top's bundles are with the inputs
            if (!sc.parent && (p.dir == model::direction::input || p.bundle != model::no_id)) continue;
            TableNextRow();
            TableNextColumn();
            TextUnformatted(p.name.c_str());
            TableNextColumn();
            auto v = sim.port_value(sc, k);
            if (p.bundle != model::no_id) {
                PushID(k);
                bundle_fields_table(ws.layout_of(p.bundle, p.flipped), &v);
                PopID();
                continue;
            }
            auto c = render::value_color(v);
            TextColored({c.r, c.g, c.b, c.a}, "%s", sim::to_string(v).c_str());
        }
        EndTable();
    }
}

void sim_view::instance_panel()
{
    using namespace ImGui;

    auto& sc = current();
    auto sch = sc.cell->get_schematic();
    if (selected < 0 || !sch || selected >= (int)sch->instances.size() || s->watched_scope() != &sc) return;
    auto& inst = sch->instances[selected];
    auto cell = ws.find_cell(inst.cell);

    std::optional<uint64_t> from;
    if (!follow_pc) from = disassemble_from;
    s->refresh_watch(memory, memory_address, 256, from, 16);
    auto w = s->watched();

    SeparatorText(("Instance " + inst.name + (cell ? " (" + cell->name + ")" : "")).c_str());
    if (!w.valid) {
        TextDisabled(sc.child(selected) ? "a subcircuit, double click to look inside" : "nothing to show");
        return;
    }

    // the state the component tells
    std::string terminal;
    bool has_terminal = false;
    if (BeginTable("state", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (auto& item : w.items) {
            if (item.v.width() == 0) {
                // texts, e.g. a terminal's screen, below the table
                if (item.name == "screen") {
                    terminal = item.text;
                    has_terminal = true;
                }
                continue;
            }
            TableNextRow();
            TableNextColumn();
            TextUnformatted(item.name.c_str());
            TableNextColumn();
            auto c = render::value_color(item.v);
            TextColored({c.r, c.g, c.b, c.a}, "%s", sim::to_string(item.v).c_str());
        }
        EndTable();
    }

    if (has_terminal) {
        SeparatorText("Terminal");
        BeginChild("screen", {0, 220}, ImGuiChildFlags_Borders);
        {
            ui::mono_scope mono;
            TextUnformatted(terminal.c_str());
        }
        if (GetScrollY() >= GetScrollMaxY() - 20) SetScrollHereY(1.0f);
        EndChild();
        SetNextItemWidth(-60);
        if (input_string("##send", terminal_input, ImGuiInputTextFlags_EnterReturnsTrue)) {
            for (char ch : terminal_input)
                s->interact(&sc, selected, "key", (uint8_t)ch);
            s->interact(&sc, selected, "key", '\r');
            terminal_input.clear();
            SetKeyboardFocusHere(-1);
        }
        SetItemTooltip("Enter sends the text and a carriage return");
        SameLine();
        if (Button("Clear")) s->interact(&sc, selected, "clear", 0);
    }

    if (!w.debug) return;

    SeparatorText("Debugger");
    if (w.pc) Text("pc %llX", (unsigned long long)*w.pc);
    if (w.pc && Button("Step instruction")) {
        s->debug([](sim::debug_interface& d) { d.step_instruction(); });
        s->run.run(true);
    }
    if (w.pc) SetItemTooltip("Runs until the processor executed one instruction");

    // breakpoints
    if (w.pc) {
        SetNextItemWidth(120);
        InputScalar("##bp", ImGuiDataType_U64, &new_breakpoint, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        SameLine();
        if (Button("Add breakpoint")) {
            auto a = new_breakpoint;
            s->debug([a](sim::debug_interface& d) { d.set_breakpoint(a, true); });
        }
        for (auto bp : w.breakpoints) {
            PushID((int)bp);
            if (SmallButton("x")) s->debug([bp](sim::debug_interface& d) { d.set_breakpoint(bp, false); });
            SameLine();
            Text("breakpoint at %llX", (unsigned long long)bp);
            PopID();
        }
    }

    // disassembly from the program counter or an address
    if (!w.disassembly.empty() || w.pc) {
        Checkbox("Follow pc", &follow_pc);
        if (!follow_pc) {
            SameLine();
            SetNextItemWidth(120);
            InputScalar("From", ImGuiDataType_U64, &disassemble_from, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal);
        }
        BeginChild("disassembly", {0, 16 * GetTextLineHeightWithSpacing()}, ImGuiChildFlags_Borders);
        {
            ui::mono_scope mono;
            for (auto& l : w.disassembly) {
                bool bp = std::find(w.breakpoints.begin(), w.breakpoints.end(), l.address) != w.breakpoints.end();
                bool at_pc = w.pc && *w.pc == l.address;
                auto label = std::string(bp ? "* " : "  ") + (at_pc ? "> " : "  ");
                char addr[32];
                std::snprintf(addr, sizeof(addr), "%04llX  ", (unsigned long long)l.address);
                PushID((int)l.address);
                if (at_pc) PushStyleColor(ImGuiCol_Text, ImVec4{1, 0.85f, 0.3f, 1});
                // a click sets or clears a breakpoint
                if (Selectable((label + addr + l.text).c_str(), false)) {
                    auto a = l.address;
                    s->debug([a, bp](sim::debug_interface& d) { d.set_breakpoint(a, !bp); });
                }
                if (at_pc) PopStyleColor();
                PopID();
            }
        }
        EndChild();
    }

    // memory as hex
    if (!w.memories.empty()) {
        std::vector<const char*> names;
        for (auto& m : w.memories)
            names.push_back(m.name.c_str());
        SetNextItemWidth(140);
        Combo("##memory", &memory, names.data(), (int)names.size());
        SameLine();
        SetNextItemWidth(120);
        if (InputScalar("Address", ImGuiDataType_U64, &memory_address, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal))
            memory_address &= ~uint64_t(15);
        SameLine();
        if (ArrowButton("##up", ImGuiDir_Up) && memory_address >= 256) memory_address -= 256;
        SameLine();
        if (ArrowButton("##down", ImGuiDir_Down)) memory_address += 256;

        if (w.memory == memory && w.memory_address == memory_address) {
            ui::mono_scope mono;
            for (size_t row = 0; row * 16 < w.bytes.size(); row++) {
                char line[128];
                int n = std::snprintf(line, sizeof(line), "%06llX ", (unsigned long long)(memory_address + row * 16));
                std::string ascii;
                for (size_t k = row * 16; k < std::min(w.bytes.size(), row * 16 + 16); k++) {
                    n += std::snprintf(line + n, sizeof(line) - n, " %02X", w.bytes[k]);
                    ascii += w.bytes[k] >= 32 && w.bytes[k] < 127 ? (char)w.bytes[k] : '.';
                }
                Text("%s  %s", line, ascii.c_str());
            }
        }
    }
}

void sim_view::hierarchy_panel()
{
    using namespace ImGui;

    auto& shown = current();

    auto node = [&](auto& self, const sim::scope& sc, int depth) -> void {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (sc.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (&sc == &shown) flags |= ImGuiTreeNodeFlags_Selected;
        if (depth < 2) flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // keep the current scope visible
        for (auto p = shown.parent; p; p = p->parent)
            if (p == &sc) SetNextItemOpen(true, ImGuiCond_Always);

        bool open = TreeNodeEx(&sc, flags, "%s", scope_label(sc).c_str());
        if (IsItemClicked() && !IsItemToggledOpen()) path = path_of(sc);
        if (BeginPopupContextItem()) {
            if (Selectable("Open in new tab")) open_in_new_tab = path_of(sc);
            EndPopup();
        }
        if (open) {
            for (auto& c : sc.children)
                self(self, *c, depth + 1);
            TreePop();
        }
    };
    node(node, s->sim().root(), 0);
}

}
