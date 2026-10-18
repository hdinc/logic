#include "sim_view.h"

#include <imgui_internal.h>

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
    const sim::scope* sc = &s->sim.root();
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
    return s->sim.root().cell->name + "/" + sc.path + " (" + sc.cell->name + ")";
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

    auto size = GetContentRegionAvail();
    size.x = std::min(size.x, (float)tex.GetWidth());
    size.y = std::min(size.y, (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    auto mouse = GetMousePos();
    glm::vec2 ndc{2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y};
    e.view.set_size({size.x, size.y});
    e.view.set_mouse(ndc);

    e.sim = &s->sim;
    e.sim_scope = &sc;
    if (hovered) e.view.handle_view_input();
    canvas_inputs(hovered, e);

    // the path may have changed, draw what is shown now
    auto& shown = current();
    auto& de = editor_of(*shown.cell);
    de.view.set_size({size.x, size.y});
    de.sim = &s->sim;
    de.sim_scope = &shown;

    fb.Bind((int)size.x, (int)size.y);
    de.draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, size.y / tex.GetHeight()}, {size.x / tex.GetWidth(), 0});

    instance_menu();
}

void sim_view::toolbar()
{
    using namespace ImGui;

    auto& sim = s->sim;

    if (Button("Reset")) {
        sim.reset();
        s->running = false;
    }
    SetItemTooltip("Back to the initial state, inputs keep their values");
    SameLine();

    if (Button("Step")) sim.tick();
    SetItemTooltip("One clock tick (Space)");
    SameLine();

    bool running = s->running;
    if (running) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (Button(running ? "Pause" : "Run")) s->running = !s->running;
    if (running) PopStyleColor();
    SetItemTooltip("Tick the clocks continuously");
    SameLine();

    SetNextItemWidth(160);
    DragFloat("##tps", &s->ticks_per_second, 0.5f, 0.5f, 1000.0f, "%.1f ticks/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    SameLine();

    Text("tick %llu", (unsigned long long)sim.ticks());
    if (sim.oscillating()) {
        SameLine();
        TextColored({1, 0.3f, 0.3f, 1}, "oscillating");
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

    auto& sim = s->sim;
    auto& sc = current();
    auto m = e.view.mouse_world();

    e.hover_tooltip();

    if (IsKeyPressed(ImGuiKey_Space)) sim.tick();
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

    // inputs of the top cell are set by the user, inside an instance they
    // come from the parent
    if (!sc.parent && IsMouseClicked(ImGuiMouseButton_Left)) {
        for (auto& h : e.hit_test(m)) {
            if (h.kind != schematic_editor::handle::port) continue;
            for (int k = 0; k < (int)sc.cell->ports.size(); k++) {
                auto& p = sc.cell->ports[k];
                if (p.id != h.id || p.dir != model::direction::input || p.width != 1) continue;
                auto v = sim.input_value(k);
                sim.set_input(k, sim::known(!(v.bits & 1), 1));
            }
        }
    }

    // a right click without dragging, a drag pans the view
    if (IsMouseReleased(ImGuiMouseButton_Right)) {
        auto d = GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        int i = instance_at();
        if (d.x * d.x + d.y * d.y < 9.0f && i >= 0 && sc.child(i)) {
            menu_instance = i;
            OpenPopup("instance");
        }
    }
}

void sim_view::instance_menu()
{
    using namespace ImGui;

    if (!BeginPopup("instance")) return;

    auto& sc = current();
    auto child = menu_instance >= 0 ? sc.child(menu_instance) : nullptr;
    if (!child) {
        CloseCurrentPopup();
    } else {
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

    auto& sim = s->sim;
    auto& sc = current();

    SeparatorText("Simulation");
    Text("Top: %s, tick %llu", sim.root().cell->name.c_str(), (unsigned long long)sim.ticks());
    if (sim.oscillating()) TextColored({1, 0.3f, 0.3f, 1}, "The circuit doesn't settle, e.g. a gate feeding itself");
    for (auto& p : sim.problems())
        TextColored({1, 0.6f, 0.2f, 1}, "%s", p.c_str());

    auto& ports = sc.cell->ports;

    if (!sc.parent) {
        SeparatorText("Inputs");
        for (int k = 0; k < (int)ports.size(); k++) {
            auto& p = ports[k];
            if (p.dir == model::direction::output) continue;
            PushID(k);
            auto v = sim.input_value(k);
            if (p.width == 1) {
                bool b = v.bits & 1;
                if (Checkbox(p.name.c_str(), &b)) sim.set_input(k, sim::known(b, 1));
            } else {
                uint64_t u = v.bits;
                if (InputScalar(p.name.c_str(), ImGuiDataType_U64, &u, nullptr, nullptr, "%llX", ImGuiInputTextFlags_CharsHexadecimal))
                    sim.set_input(k, sim::known(u, p.width));
                SetItemTooltip("hex, %d bits", p.width);
            }
            PopID();
        }
    } else {
        SeparatorText("Instance");
        Text("%s", title().c_str());
        TextDisabled("ports are driven by the parent");
    }

    SeparatorText(sc.parent ? "Ports" : "Outputs");
    if (BeginTable("ports", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        for (int k = 0; k < (int)ports.size(); k++) {
            auto& p = ports[k];
            if (!sc.parent && p.dir == model::direction::input) continue;
            TableNextRow();
            TableNextColumn();
            TextUnformatted(p.name.c_str());
            TableNextColumn();
            auto v = sim.port_value(sc, k);
            auto c = render::value_color(v, p.width);
            TextColored({c.r, c.g, c.b, c.a}, "%s", sim::to_string(v, p.width).c_str());
        }
        EndTable();
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
    node(node, s->sim.root(), 0);
}

}
