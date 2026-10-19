#pragma once

#include "../model/workspace.h"
#include "../render/renderer.h"

#include <Hd/FrameBuffer.h>
#include <Hd/Texture.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace logic::app {

// the cells of all libraries as icons drawn from their symbols, grouped by
// category (builtins) or library. vertical: collapsible groups wrapping
// to the width, horizontal: one scrolling row for docking above the editor.
class component_bar {
public:
    enum class orientation {
        vertical,
        horizontal
    };

    explicit component_bar(render::renderer& r);

    // what the bar places into
    struct target {
        // can the cell be placed now, null if there's no schematic to place into
        std::function<bool(model::id)> can_place;
        // the cell being placed, highlighted
        model::id placing = model::no_id;
    };

    // draws the "Components" window, returns the clicked cell or no_id
    model::id gui(const model::workspace& ws, const target& t);

    orientation dir = orientation::vertical;
    // set when dir changes from the bar's context menu
    bool dir_changed = false;

    static constexpr int icon_size = 40;

private:
    struct group {
        std::string name;
        std::vector<const model::cell*> cells;
    };
    std::vector<group> groups(const model::workspace& ws) const;

    // draws the icons of the cells into the atlas, one slot each in order
    void draw_icons(const std::vector<const model::cell*>& cells);
    // returns true if clicked
    bool icon(const model::workspace& ws, const model::cell& c, int slot, const target& t);
    void context_menu();

    render::renderer& r;
    Hd::Texture atlas;
    Hd::FrameBuffer atlas_fb;

    std::string search;
    // groups closed in the vertical bar
    std::set<std::string> closed;
};

}
