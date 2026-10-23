#include "component_bar.h"
#include "../editor/context.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>

namespace logic::app {

namespace {

constexpr int atlas_size = 1024;
constexpr int slots_per_row = atlas_size / component_bar::icon_size;
constexpr int max_slots = slots_per_row * slots_per_row;

bool contains_ignore_case(const std::string& s, const std::string& sub)
{
    auto it = std::search(s.begin(), s.end(), sub.begin(), sub.end(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
    return it != s.end();
}

glm::ivec2 slot_pos(int slot)
{
    return {slot % slots_per_row * component_bar::icon_size, slot / slots_per_row * component_bar::icon_size};
}

}

component_bar::component_bar(render::renderer& rr)
    : r(rr)
    , atlas(Hd::Texture::emptyTexture(atlas_size, atlas_size))
    , atlas_fb(atlas)
{
    // drawn 1:1, the mipmaps made when the texture was created are never
    // updated
    glBindTexture(GL_TEXTURE_2D, atlas.Id());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::vector<component_bar::group> component_bar::groups(const model::workspace& ws) const
{
    std::vector<group> gs;
    auto add = [&](const std::string& name, const model::cell* c) {
        auto it = std::find_if(gs.begin(), gs.end(), [&](auto& g) { return g.name == name; });
        if (it == gs.end()) it = gs.insert(gs.end(), group{name, {}});
        it->cells.push_back(c);
    };

    // builtins first, then the other libraries' cells, each by category or
    // else by library
    auto add_cells = [&](bool builtins) {
        for (auto& lib : ws.libraries) {
            for (auto& c : lib->cells) {
                if ((ws.primitive_of(*c) != nullptr) != builtins) continue;
                auto& cat = c->category;
                if (!search.empty() && !contains_ignore_case(c->name, search) && !contains_ignore_case(cat, search) && !contains_ignore_case(lib->name, search))
                    continue;
                add(cat.empty() ? lib->name : cat, c.get());
            }
        }
    };
    add_cells(true);
    add_cells(false);
    return gs;
}

void component_bar::draw_icons(const std::vector<const model::cell*>& cells)
{
    auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    glm::vec4 color{text.x, text.y, text.z, text.w};

    GLint old_viewport[4];
    glGetIntegerv(GL_VIEWPORT, old_viewport);

    atlas_fb.Bind();
    // transparent, in the icon color so the blended edges keep it
    glClearColor(color.r, color.g, color.b, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const float pad = 4.0f;
    for (size_t k = 0; k < cells.size() && k < (size_t)max_slots; k++) {
        auto& sym = cells[k]->symbol;
        auto b = sym.bounds();
        glm::vec2 lo = b.min, hi = b.max;
        glm::vec2 mid = (lo + hi) * 0.5f;
        float extent = std::max({hi.x - lo.x, hi.y - lo.y, 10.0f});
        float half = extent / 2 * icon_size / (icon_size - 2 * pad);

        auto p = slot_pos((int)k);
        glViewport(p.x, p.y, icon_size, icon_size);
        auto vp = glm::ortho(mid.x - half, mid.x + half, mid.y - half, mid.y + half);

        r.g.begin(vp);
        // lines of about 1.5 pixels whatever the symbol's size
        r.stroke_scale = std::max(1.0f, 1.5f * r.pixel() / render::stroke);
        // no pin dots, nothing is connected in a preview
        std::vector<bool> connected(sym.pins.size(), true);
        render::draw_symbol(r, sym, {0, 0}, {}, color, &connected);
        // a new cell without a symbol yet
        if (sym.shapes.empty() && sym.pins.empty()) r.rect_outline(lo + 2.0f, hi - 2.0f, color, render::stroke * r.stroke_scale, 2.0f);
        r.g.end();
    }
    r.stroke_scale = 1.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
}

bool component_bar::icon(const model::workspace& ws, const model::cell& c, int slot, const target& t)
{
    using namespace ImGui;

    if (slot >= max_slots) return false;

    auto p = slot_pos(slot);
    // gl textures start at the bottom
    ImVec2 uv0{(float)p.x / atlas_size, (float)(p.y + icon_size) / atlas_size};
    ImVec2 uv1{(float)(p.x + icon_size) / atlas_size, (float)p.y / atlas_size};

    bool can = t.can_place && t.can_place(c.id);
    bool active = t.placing == c.id;

    PushID(&c);
    if (active) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    else PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    ImVec4 tint{1, 1, 1, can ? 1.0f : 0.35f};
    bool clicked = ImageButton("icon", (ImTextureID)atlas.Id(), {(float)icon_size, (float)icon_size}, uv0, uv1, {0, 0, 0, 0}, tint);
    PopStyleColor();
    PopID();

    if (BeginItemTooltip()) {
        TextUnformatted(c.name.c_str());
        auto pr = ws.primitive_of(c);
        if (pr && !pr->description().empty()) TextDisabled("%s", pr->description().c_str());
        if (!t.can_place)
            TextDisabled("open a schematic to place it");
        else if (!can)
            TextDisabled("can't be placed here now");
        EndTooltip();
    }

    return clicked && can;
}

void component_bar::context_menu()
{
    using namespace ImGui;

    if (BeginPopupContextWindow("bar_menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (MenuItem("Vertical", nullptr, dir == orientation::vertical)) {
            dir_changed = dir != orientation::vertical;
            dir = orientation::vertical;
        }
        if (MenuItem("Horizontal", nullptr, dir == orientation::horizontal)) {
            dir_changed = dir != orientation::horizontal;
            dir = orientation::horizontal;
        }
        EndPopup();
    }
}

model::id component_bar::gui(const model::workspace& ws, const target& t)
{
    using namespace ImGui;

    model::id clicked = model::no_id;
    bool horizontal = dir == orientation::horizontal;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (horizontal) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!Begin("Components", nullptr, flags)) {
        End();
        return clicked;
    }
    context_menu();

    auto gs = groups(ws);
    std::vector<const model::cell*> cells;
    for (auto& g : gs)
        cells.insert(cells.end(), g.cells.begin(), g.cells.end());
    draw_icons(cells);

    auto& style = GetStyle();
    float button = icon_size + 2 * style.FramePadding.x;
    int slot = 0;

    if (horizontal) {
        SetNextItemWidth(160);
        editor::input_string("##search", search);
        if (search.empty() && !IsItemActive()) {
            // placeholder
            auto min = GetItemRectMin();
            GetWindowDrawList()->AddText({min.x + style.FramePadding.x, min.y + style.FramePadding.y}, GetColorU32(ImGuiCol_TextDisabled), "Search...");
        }
        SameLine();

        BeginChild("row", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        bool first = true;
        for (auto& g : gs) {
            if (!first) {
                SameLine();
                SeparatorEx(ImGuiSeparatorFlags_Vertical);
                SameLine();
            }
            for (auto c : g.cells) {
                if (!first) SameLine();
                first = false;
                if (icon(ws, *c, slot++, t)) clicked = c->id;
            }
        }
        if (gs.empty()) TextDisabled("nothing found");
        // the wheel scrolls the row
        if (IsWindowHovered() && GetIO().MouseWheel != 0) SetScrollX(GetScrollX() - GetIO().MouseWheel * button);
        EndChild();
    } else {
        SetNextItemWidth(-FLT_MIN);
        editor::input_string("##search", search);
        if (search.empty() && !IsItemActive()) {
            auto min = GetItemRectMin();
            GetWindowDrawList()->AddText({min.x + style.FramePadding.x, min.y + style.FramePadding.y}, GetColorU32(ImGuiCol_TextDisabled), "Search components...");
        }

        float avail = GetContentRegionAvail().x;
        int per_row = std::max(1, (int)((avail + style.ItemSpacing.x) / (button + style.ItemSpacing.x)));

        for (auto& g : gs) {
            Separator();
            // groups stay open while searching
            bool open = !search.empty() || !closed.count(g.name);
            SetNextItemOpen(open);
            if (!TreeNodeEx(g.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
                if (search.empty()) closed.insert(g.name);
                slot += (int)g.cells.size();
                continue;
            }
            if (search.empty()) closed.erase(g.name);

            for (size_t k = 0; k < g.cells.size(); k++) {
                if (k % per_row) SameLine();
                if (icon(ws, *g.cells[k], slot++, t)) clicked = g.cells[k]->id;
            }
        }
        if (gs.empty()) TextDisabled("nothing found");
    }

    End();
    return clicked;
}

}
