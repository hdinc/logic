#pragma once

#include "canvas.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace logic::editor {

// a canvas in the remaining space of the current imgui window. input(hovered)
// runs after the view got its size and mouse, hovered when the mouse is on
// it and the window has the focus. draw() then draws into fb, which is shown
// 1:1 in whole pixels so small text stays sharp. min/max get the screen
// rectangle. false if there's no room.
template <class Input, class Draw>
bool canvas_widget(canvas& c, Hd::Texture& tex, Hd::FrameBuffer& fb, Input input, Draw draw, ImVec2* min = nullptr, ImVec2* max = nullptr)
{
    using namespace ImGui;

    auto size = GetContentRegionAvail();
    size.x = std::min(std::floor(size.x), (float)tex.GetWidth());
    size.y = std::min(std::floor(size.y), (float)tex.GetHeight());
    if (size.x < 1.0f || size.y < 1.0f) return false;

    auto screen_pos = GetCursorScreenPos();
    auto cursor = GetCursorPos();
    InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = IsItemHovered() && IsWindowFocused();

    auto mouse = GetMousePos();
    c.begin_frame({size.x, size.y});
    c.set_mouse({2.0f * (mouse.x - screen_pos.x) / size.x - 1.0f, 1.0f - 2.0f * (mouse.y - screen_pos.y) / size.y});
    input(hovered);

    fb.Bind((int)size.x, (int)size.y);
    draw();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // the texture is bigger than the view, only show the drawn corner
    SetCursorPos(cursor);
    Image((ImTextureID)tex.Id(), size, {0, size.y / tex.GetHeight()}, {size.x / tex.GetWidth(), 0});
    if (min) *min = screen_pos;
    if (max) *max = {screen_pos.x + size.x, screen_pos.y + size.y};
    return true;
}

}
