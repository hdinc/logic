#pragma once

#include "../model/workspace.h"
#include "../render/renderer.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

namespace logic::app {

// a few of the builtin cells as small icons (see model::primitive::icon), in
// a translucent strip over the schematic canvas along one of its edges.
// grouped by category with separators, no scrollbar: it scrolls when the
// mouse is near one of its ends or with the wheel.
class component_bar {
public:
    // the canvas edge the strip is on, or off
    enum class edge {
        top,
        bottom,
        left,
        right,
        off
    };

    explicit component_bar(render::renderer& r);

    // what the bar places into
    struct target {
        // can the cell be placed now
        std::function<bool(model::id)> can_place;
        // the cell being placed, highlighted
        model::id placing = model::no_id;
    };

    // draws the strip over the canvas, in the window the canvas is in and
    // after it. returns the clicked cell or no_id.
    model::id gui(const model::workspace& ws, const target& t, ImVec2 canvas_min, ImVec2 canvas_max);

    edge side = edge::top;

    // the icons' size in pixels, chosen in the bar's menu
    enum class icon_size {
        small,
        medium,
        large
    };
    icon_size size = icon_size::small;
    int icon_px() const;
    // the atlas holds icons up to this size
    static constexpr int slot_size = 48;

private:
    struct group {
        std::string name;
        std::vector<const model::cell*> cells;
    };
    std::vector<group> groups(const model::workspace& ws) const;

    // draws the icons of the cells into the atlas, one slot each in order
    void draw_icons(const model::workspace& ws, const std::vector<const model::cell*>& cells);
    // returns true if clicked
    bool icon(const model::workspace& ws, const model::cell& c, int slot, const target& t);
    void context_menu();
    // edge and wheel scrolling, inside the strip's child window
    void scroll_strip(bool row, float button);

    render::renderer& r;
    Hd::Texture atlas;
    Hd::FrameBuffer atlas_fb;

    // shown scroll, and where it eases to
    float scroll = 0;
    float scroll_target = 0;
    // the strip's opacity, eased towards more while hovered
    float alpha = 0;
    bool hovered = false;
};

}
