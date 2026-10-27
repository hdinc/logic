#include "icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace logic::ui {

namespace {

// the box of the glyph's visible pixels, relative to where the text is
// drawn. the glyph's quad comes from the outline's control points, which
// stick out past curves (e.g. the arc of undo), so centering the quad leaves
// such icons a pixel off. the atlas keeps its pixels in memory, a glyph is a
// few hundred of them.
ImRect ink_box(const ImFontGlyph& g)
{
    ImRect quad{g.X0, g.Y0, g.X1, g.Y1};
    auto tex = ImGui::GetIO().Fonts->TexData;
    if (!tex || !tex->Pixels) return quad;
    int x0 = (int)ImFloor(g.U0 * tex->Width + 0.5f), x1 = (int)ImFloor(g.U1 * tex->Width + 0.5f);
    int y0 = (int)ImFloor(g.V0 * tex->Height + 0.5f), y1 = (int)ImFloor(g.V1 * tex->Height + 0.5f);
    if (x1 <= x0 || y1 <= y0 || x1 > tex->Width || y1 > tex->Height) return quad;

    // more than a quarter covered counts as drawn
    int minx = x1, maxx = x0 - 1, miny = y1, maxy = y0 - 1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            auto p = tex->Pixels + (size_t(y) * tex->Width + x) * tex->BytesPerPixel;
            int alpha = tex->Format == ImTextureFormat_Alpha8 ? p[0] : p[3];
            if (alpha < 64) continue;
            minx = ImMin(minx, x), maxx = ImMax(maxx, x);
            miny = ImMin(miny, y), maxy = ImMax(maxy, y);
        }
    }
    if (maxx < minx) return quad;
    // atlas pixels to the quad's coordinates
    float sx = (g.X1 - g.X0) / float(x1 - x0), sy = (g.Y1 - g.Y0) / float(y1 - y0);
    return {g.X0 + (minx - x0) * sx, g.Y0 + (miny - y0) * sy, g.X0 + (maxx + 1 - x0) * sx, g.Y0 + (maxy + 1 - y0) * sy};
}

}

namespace {

toolbar_size g_size = toolbar_size::small;

}

toolbar_size get_toolbar_size()
{
    return g_size;
}

void set_toolbar_size(toolbar_size s)
{
    g_size = s;
}

float toolbar_button_size()
{
    float px = g_size == toolbar_size::large ? 48.0f : g_size == toolbar_size::medium ? 38.0f : 30.0f;
    return std::max(px, ImGui::GetFrameHeight());
}

float toolbar_gap()
{
    return 3.0f;
}

void toolbar_size_menu()
{
    using namespace ImGui;
    const char* names[] = {"Small icons", "Medium icons", "Large icons"};
    for (int k = 0; k < 3; k++)
        if (MenuItem(names[k], nullptr, g_size == (toolbar_size)k)) g_size = (toolbar_size)k;
}

bool icon_button(const char* icon, const char* tip, bool active, bool enabled)
{
    using namespace ImGui;
    if (active) PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));
    BeginDisabled(!enabled);
    float h = toolbar_button_size();
    // padding that puts text on the same line in the middle of the button
    PushStyleVar(ImGuiStyleVar_FramePadding, {GetStyle().FramePadding.x, (h - GetFontSize()) / 2});
    PushID(icon);
    bool clicked = Button("##icon", {h, h});
    PopStyleVar();
    // drawn by hand: a label would be centered by its advance and line
    // height, which leaves icons a pixel or two off. the icon font's em is
    // 60% of the button, font awesome's icons fill most of it.
    auto min = GetItemRectMin(), max = GetItemRectMax();
    float size = ImFloor(0.6f * h);
    unsigned int c = 0;
    ImTextCharFromUtf8(&c, icon, nullptr);
    if (auto g = GetFont()->GetFontBaked(size)->FindGlyphNoFallback((ImWchar)c)) {
        auto ink = ink_box(*g);
        ImVec2 at{ImFloor((min.x + max.x - ink.Min.x - ink.Max.x) / 2 + 0.5f), ImFloor((min.y + max.y - ink.Min.y - ink.Max.y) / 2 + 0.5f)};
        GetWindowDrawList()->AddText(GetFont(), size, at, GetColorU32(ImGuiCol_Text), icon);
    }
    EndDisabled();
    if (active) PopStyleColor();
    SetItemTooltip("%s", tip);
    if (BeginPopupContextItem("size")) {
        toolbar_size_menu();
        EndPopup();
    }
    PopID();
    return clicked;
}

void toolbar_center()
{
    using namespace ImGui;
    SetCursorPosY(GetCursorPosY() + std::max(0.0f, (toolbar_button_size() - GetFrameHeight()) / 2));
}

}
