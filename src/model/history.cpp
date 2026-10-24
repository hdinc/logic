#include "history.h"

#include <algorithm>

namespace logic::model {

cell_state save_state(const cell& c)
{
    return {c.ports, c.symbol, c.impl, c.next_id};
}

namespace {

// the widths of bundle ports follow their types, which have no undo here
bool same_ports(const std::vector<port>& a, const std::vector<port>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); k++) {
        if (a[k] == b[k]) continue;
        if (a[k].bundle == no_id || a[k].bundle != b[k].bundle) return false;
        auto x = a[k];
        x.width = b[k].width;
        if (!(x == b[k])) return false;
    }
    return true;
}

}

void restore_state(cell& c, const cell_state& s)
{
    // bundle ports keep the width of their type
    auto old = std::move(c.ports);
    c.ports = s.ports;
    for (auto& p : c.ports) {
        if (p.bundle == no_id) continue;
        auto o = std::find_if(old.begin(), old.end(), [&](const port& x) { return x.id == p.id && x.bundle == p.bundle; });
        if (o != old.end()) p.width = o->width;
    }
    c.symbol = s.symbol;
    c.impl = s.impl;
    c.next_id = s.next_id;
}

history::history(const cell& c, size_t l)
    : current(save_state(c))
    , limit(l)
{
}

bool history::unchanged(const cell& c) const
{
    // compared in place, commit() runs every frame and should not copy the cell
    return same_ports(c.ports, current.ports) && c.symbol == current.symbol && c.impl == current.impl && c.next_id == current.next_id;
}

bool history::commit(const cell& c)
{
    if (unchanged(c)) return false;

    undo_stack.push_back(std::move(current));
    if (undo_stack.size() > limit) undo_stack.pop_front();
    redo_stack.clear();
    current = save_state(c);
    return true;
}

bool history::undo(cell& c)
{
    commit(c);
    if (undo_stack.empty()) return false;

    redo_stack.push_back(std::move(current));
    current = std::move(undo_stack.back());
    undo_stack.pop_back();
    restore_state(c, current);
    restores++;
    return true;
}

bool history::redo(cell& c)
{
    // changes after an undo replace the redo steps
    if (commit(c) || redo_stack.empty()) return false;

    undo_stack.push_back(std::move(current));
    current = std::move(redo_stack.back());
    redo_stack.pop_back();
    restore_state(c, current);
    restores++;
    return true;
}

}
