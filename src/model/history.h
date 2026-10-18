#pragma once

#include "cell.h"

#include <cstddef>
#include <deque>
#include <vector>

namespace logic::model {

// the part of a cell that editing changes. id and name are not included,
// renaming is done in the library panel and can't be undone.
struct cell_state {
    std::vector<port> ports;
    model::symbol symbol;
    implementation impl;
    id next_id = 1;

    bool operator==(const cell_state&) const = default;
};

cell_state save_state(const cell& c);
void restore_state(cell& c, const cell_state& s);

// undo and redo for one cell with snapshots of its state. the cell is edited
// directly, commit() records the changes made since the last commit as one
// undo step.
class history {
public:
    explicit history(const cell& c, size_t limit = 200);

    // returns true if the cell changed since the last commit
    bool commit(const cell& c);

    bool can_undo() const { return !undo_stack.empty(); }
    bool can_redo() const { return !redo_stack.empty(); }

    // uncommitted changes are committed first, so they are undone as well.
    // returns false if there was nothing to undo / redo.
    bool undo(cell& c);
    bool redo(cell& c);

private:
    bool unchanged(const cell& c) const;

    cell_state current;
    std::deque<cell_state> undo_stack;
    std::vector<cell_state> redo_stack;
    size_t limit;
};

}
