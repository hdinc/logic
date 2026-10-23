#pragma once

#include "../model/workspace.h"

#include <deque>
#include <vector>

namespace logic::editor {

// the fields of a bundle type as a table. a change updates the ports of
// that type everywhere (see workspace::bundles_changed). it has its own
// undo, the cells' undo doesn't cover their bundle types.
class bundle_editor {
public:
    bundle_editor(model::workspace& ws, model::library& lib, model::bundle_type& b);

    model::bundle_type& bundle() { return b; }
    model::library& library() { return lib; }

    // draws into the remaining space of the current imgui window. locked
    // while a simulation runs, its cells must not change.
    void gui(bool locked);

    bool can_undo() const { return !undo_stack.empty() || !unchanged(); }
    bool can_redo() const { return !redo_stack.empty(); }
    void undo();
    void redo();

    // problems with the fields, e.g. duplicate names
    std::vector<std::string> check() const;

private:
    void changed();
    bool unchanged() const;
    void commit();
    void restore();

    model::workspace& ws;
    model::library& lib;
    model::bundle_type& b;

    model::bundle_type current;
    std::deque<model::bundle_type> undo_stack;
    std::vector<model::bundle_type> redo_stack;
};

}
