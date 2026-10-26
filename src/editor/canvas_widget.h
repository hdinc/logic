#pragma once

#include "canvas.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace logic::editor {

// a canvas's size in the remaining space of the current imgui window, in
// whole imgui units, and in framebuffer pixels: imgui units times the
// display's scale (more than 1 on hi-dpi screens). the framebuffer is drawn
// at the screen's resolution and shown 1:1, so it isn't stretched and small
// text stays sharp. both are 0 if there's no room or tex is too small.
struct canvas_size {
    ImVec2 size;
    ImVec2 pixels;
};

inline canvas_size canvas_room(Hd::Texture& tex)
{
    float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    auto size = ImGui::GetContentRegionAvail();
    size.x = std::min(std::floor(size.x), std::floor(tex.GetWidth() / scale));
    size.y = std::min(std::floor(size.y), std::floor(tex.GetHeight() / scale));
    if (size.x < 1.0f || size.y < 1.0f) return {};
    return {size, {std::round(size.x * scale), std::round(size.y * scale)}};
}

// a canvas in the remaining space of the current imgui window. input(hovered)
// runs after the view got its size and mouse, hovered when the mouse is on
// it and the window has the focus. draw() then draws into fb, see
// canvas_room. min/max get the screen rectangle. false if there's no room.
template <class Input, class Draw>
bool canvas_widget(canvas& c, Hd::Texture& tex, Hd::FrameBuffer& fb, Input input, Draw draw, ImVec2* min = nullptr, ImVec2* max = nullptr)
{
    using namespace ImGui;

    auto [size, px] = canvas_room(tex);
    if (size.x < 1.0f) return false;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    auto mouse = GetMousePos();
    c.begin_frame({size.x, size.y});
    c.set_mouse({2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y});
    input(hovered);

    fb.Bind((int)px.x, (int)px.y);
    draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // the texture is bigger than the view, only show the drawn corner
    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, px.y / tex.GetHeight()}, {px.x / tex.GetWidth(), 0});
    if (min) *min = screen_pos;
    if (max) *max = {screen_pos.x + size.x, screen_pos.y + size.y};
    return true;
}

}
