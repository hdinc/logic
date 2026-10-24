#include "component_bar.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace logic::app {

namespace {

constexpr int atlas_size = 1024;
constexpr int slots_per_row = atlas_size / component_bar::slot_size;
constexpr int max_slots = slots_per_row * slots_per_row;

// the builtins on the bar, the rest are placed from the library panel or
// with space
const std::set<std::string> shown_kinds = {
    "constant", "clock",
    "and", "or", "xor", "not", "nand", "nor", "buffer", "tristate",
    "mux",
    "splitter", "tunnel",
    "register", "counter",
    "led", "button", "switch", "seg7_display", "hex_display",
};

// space between the strip and the canvas edge, and inside the strip
constexpr float margin = 8.0f;
constexpr float pad = 4.0f;
// a group separator: a line with this much room on both sides
constexpr float separator_gap = 6.0f;
// the ends where the mouse scrolls the strip, in buttons, and the speed at
// the very end in pixels per second
constexpr float edge_zone = 1.5f;
constexpr float edge_speed = 700.0f;
constexpr float fade = 16.0f;

glm::ivec2 slot_pos(int slot)
{
    return {slot % slots_per_row * component_bar::slot_size, slot / slots_per_row * component_bar::slot_size};
}

// half the size of the box icons are drawn in, with a little margin
constexpr float icon_half = 13.0f;

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

    // a subset of the builtins, by category in the order they are registered
    for (auto& lib : ws.libraries) {
        for (auto& c : lib->cells) {
            auto prim = ws.primitive_of(*c);
            if (!prim || !shown_kinds.count(prim->kind())) continue;
            add(c->category.empty() ? lib->name : c->category, c.get());
        }
    }
    return gs;
}

int component_bar::icon_px() const
{
    switch (size) {
    case icon_size::medium: return 32;
    case icon_size::large: return 44;
    default: return 24;
    }
}

void component_bar::draw_icons(const model::workspace& ws, const std::vector<const model::cell*>& cells)
{
    auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    glm::vec4 color{text.x, text.y, text.z, text.w};

    GLint old_viewport[4];
    glGetIntegerv(GL_VIEWPORT, old_viewport);

    atlas_fb.Bind();
    // transparent, in the icon color so the blended edges keep it
    glClearColor(color.r, color.g, color.b, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int px = icon_px();
    for (size_t k = 0; k < cells.size() && k < (size_t)max_slots; k++) {
        // the part's icon, or its symbol if it has none
        auto prim = ws.primitive_of(*cells[k]);
        auto icon = prim ? prim->icon() : model::symbol{};
        bool has_icon = !icon.shapes.empty();
        auto& sym = has_icon ? icon : cells[k]->symbol;

        glm::vec2 mid{0, 0};
        float half = icon_half;
        if (!has_icon) {
            auto b = sym.bounds();
            mid = (b.min + b.max) * 0.5f;
            half = std::max({b.max.x - b.min.x, b.max.y - b.min.y, 10.0f}) / 2 * 1.1f;
        }

        auto p = slot_pos((int)k);
        glViewport(p.x, p.y, px, px);
        auto vp = glm::ortho(mid.x - half, mid.x + half, mid.y - half, mid.y + half);

        r.g.begin(vp);
        // thin lines, about 1.3 pixels at the small size and 1.8 at most
        float line_px = std::clamp(px / 24.0f, 1.3f, 1.8f);
        r.stroke_scale = line_px * r.pixel() / render::stroke;
        // no pin dots, icons have no pins and a symbol shows its body
        std::vector<bool> connected(sym.pins.size(), true);
        render::draw_symbol(r, sym, {0, 0}, {}, color, &connected);
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
    int px = icon_px();
    // gl textures start at the bottom
    ImVec2 uv0{(float)p.x / atlas_size, (float)(p.y + px) / atlas_size};
    ImVec2 uv1{(float)(p.x + px) / atlas_size, (float)p.y / atlas_size};

    bool can = t.can_place && t.can_place(c.id);
    bool active = t.placing == c.id;

    PushID(&c);
    PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(active ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
    ImVec4 tint{1, 1, 1, can ? 1.0f : 0.35f};
    bool clicked = ImageButton("icon", (ImTextureID)atlas.Id(), {(float)px, (float)px}, uv0, uv1, {0, 0, 0, 0}, tint);
    PopStyleColor();
    PopID();

    // the tooltip isn't translucent like the strip
    PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (BeginItemTooltip()) {
        TextUnformatted(c.name.c_str());
        auto pr = ws.primitive_of(c);
        if (pr && !pr->description().empty()) TextDisabled("%s", pr->description().c_str());
        if (!can) TextDisabled("can't be placed here now");
        EndTooltip();
    }
    PopStyleVar();

    return clicked && can;
}

void component_bar::context_menu()
{
    using namespace ImGui;

    PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (BeginPopupContextWindow("bar_menu")) {
        const char* edges[] = {"Top", "Bottom", "Left", "Right"};
        for (int k = 0; k < 4; k++)
            if (MenuItem(edges[k], nullptr, side == (edge)k)) side = (edge)k;
        if (MenuItem("Hide")) side = edge::off;
        SetItemTooltip("View > Component bar shows it again");
        Separator();
        const char* names[] = {"Small icons", "Medium icons", "Large icons"};
        for (int k = 0; k < 3; k++)
            if (MenuItem(names[k], nullptr, size == (icon_size)k)) size = (icon_size)k;
        EndPopup();
    }
    PopStyleVar();
}

void component_bar::scroll_strip(bool row, float button)
{
    using namespace ImGui;

    auto& io = GetIO();
    float dt = io.DeltaTime;
    float max = row ? GetScrollMaxX() : GetScrollMaxY();

    if (hovered && max > 0) {
        // near an end: towards it, faster closer to the edge
        auto pos = GetWindowPos();
        auto size = GetWindowSize();
        float m = row ? io.MousePos.x - pos.x : io.MousePos.y - pos.y;
        float len = row ? size.x : size.y;
        float zone = edge_zone * button;
        float v = 0;
        if (m < zone)
            v = -(1 - m / zone);
        else if (m > len - zone)
            v = 1 - (len - m) / zone;
        scroll_target += v * edge_speed * dt;
        // jumps on the target, the shown scroll follows smoothly
        float wheel = io.MouseWheel + (row ? -io.MouseWheelH : 0);
        scroll_target -= wheel * 2 * button;
    }
    scroll_target = std::clamp(scroll_target, 0.0f, max);
    scroll += (scroll_target - scroll) * (1 - std::exp(-15 * dt));
    if (std::abs(scroll_target - scroll) < 0.5f) scroll = scroll_target;
    scroll = std::clamp(scroll, 0.0f, max);
    if (row)
        SetScrollX(scroll);
    else
        SetScrollY(scroll);

    // fades where more icons are, over them in the strip's color
    auto pos = GetWindowPos();
    auto size = GetWindowSize();
    auto dl = GetWindowDrawList();
    auto solid = GetColorU32(ImGuiCol_WindowBg);
    auto clear = GetColorU32(ImGuiCol_WindowBg, 0.0f);
    PushClipRect(pos, {pos.x + size.x, pos.y + size.y}, false);
    if (scroll > 0.5f) {
        if (row)
            dl->AddRectFilledMultiColor(pos, {pos.x + fade, pos.y + size.y}, solid, clear, clear, solid);
        else
            dl->AddRectFilledMultiColor(pos, {pos.x + size.x, pos.y + fade}, solid, solid, clear, clear);
    }
    if (scroll < max - 0.5f) {
        if (row)
            dl->AddRectFilledMultiColor({pos.x + size.x - fade, pos.y}, {pos.x + size.x, pos.y + size.y}, clear, solid, solid, clear);
        else
            dl->AddRectFilledMultiColor({pos.x, pos.y + size.y - fade}, {pos.x + size.x, pos.y + size.y}, clear, clear, solid, solid);
    }
    PopClipRect();
}

model::id component_bar::gui(const model::workspace& ws, const target& t, ImVec2 canvas_min, ImVec2 canvas_max)
{
    using namespace ImGui;

    model::id clicked = model::no_id;
    if (side == edge::off) return clicked;
    bool row = side == edge::top || side == edge::bottom;

    auto gs = groups(ws);
    std::vector<const model::cell*> cells;
    for (auto& g : gs)
        cells.insert(cells.end(), g.cells.begin(), g.cells.end());
    if (cells.empty()) return clicked;
    draw_icons(ws, cells);

    // buttons along the strip, their length and thickness
    auto& style = GetStyle();
    ImVec2 button{icon_px() + 2 * style.FramePadding.x, icon_px() + 2 * style.FramePadding.y};
    float step = row ? button.x : button.y;
    float thick = row ? button.y : button.x;
    float spacing = row ? style.ItemSpacing.x : style.ItemSpacing.y;
    float content = cells.size() * step + (cells.size() - 1) * spacing + (gs.size() - 1) * 2 * separator_gap;

    // as long as the icons, at most the canvas minus the margins
    ImVec2 canvas{canvas_max.x - canvas_min.x, canvas_max.y - canvas_min.y};
    float len = std::min(content + 2 * pad, (row ? canvas.x : canvas.y) - 2 * margin);
    if (len < step + 2 * pad || (row ? canvas.y : canvas.x) < thick + 2 * pad + 2 * margin) return clicked;
    ImVec2 size = row ? ImVec2{len, thick + 2 * pad} : ImVec2{thick + 2 * pad, len};

    // centered along its edge, whole pixels so the icons stay sharp
    ImVec2 pos{canvas_min.x + margin, canvas_min.y + margin};
    if (side == edge::bottom) pos.y = canvas_max.y - margin - size.y;
    if (side == edge::right) pos.x = canvas_max.x - margin - size.x;
    if (row)
        pos.x = std::floor((canvas_min.x + canvas_max.x - size.x) / 2);
    else
        pos.y = std::floor((canvas_min.y + canvas_max.y - size.y) / 2);

    // faint over the canvas, nearly solid while the mouse is on it
    float target_alpha = hovered ? 0.95f : 0.55f;
    alpha += (target_alpha - alpha) * (1 - std::exp(-GetIO().DeltaTime / 0.05f));

    SetCursorScreenPos(pos);
    PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
    PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    PushStyleColor(ImGuiCol_ChildBg, GetStyleColorVec4(ImGuiCol_WindowBg));
    BeginChild("component_bar", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    hovered = IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    context_menu();

    float at = pad;
    int slot = 0;
    auto sep = GetColorU32(ImGuiCol_Separator);
    for (size_t k = 0; k < gs.size(); k++) {
        if (k > 0) {
            // a line between groups, in the middle of the gap
            float mid = at - spacing / 2 + separator_gap;
            auto wp = GetWindowPos();
            float s = row ? GetScrollX() : GetScrollY();
            if (row)
                GetWindowDrawList()->AddLine({wp.x + mid - s, wp.y + pad}, {wp.x + mid - s, wp.y + pad + thick}, sep);
            else
                GetWindowDrawList()->AddLine({wp.x + pad, wp.y + mid - s}, {wp.x + pad + thick, wp.y + mid - s}, sep);
            at += 2 * separator_gap;
        }
        for (auto c : gs[k].cells) {
            SetCursorPos(row ? ImVec2{at, pad} : ImVec2{pad, at});
            if (icon(ws, *c, slot++, t)) clicked = c->id;
            at += step + spacing;
        }
    }

    scroll_strip(row, step);
    EndChild();
    PopStyleColor();
    PopStyleVar(3);

    // the canvas window gets the keys again (esc, r, m) and the next click
    if (hovered && IsMouseReleased(ImGuiMouseButton_Left)) SetWindowFocus();
    return clicked;
}

}
