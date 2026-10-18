// tests for the model and io layers. no test framework, a failing check
// prints the location and the test exits with 1.

#include "../src/io/json.h"
#include "../src/io/storage.h"
#include "../src/model/history.h"
#include "../src/model/workspace.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace logic;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static bool has_wire(const std::vector<model::wire>& ws, glm::ivec2 a, glm::ivec2 b)
{
    return std::any_of(ws.begin(), ws.end(), [&](auto& w) { return (w.a == a && w.b == b) || (w.a == b && w.b == a); });
}

static void test_normalize_wires()
{
    model::id next = 1;

    // overlapping collinear wires merge into one
    auto r = model::normalize_wires({{1, {0, 0}, {30, 0}}, {2, {20, 0}, {50, 0}}}, {}, next);
    CHECK(r.size() == 1);
    CHECK(has_wire(r, {0, 0}, {50, 0}));

    // T junction splits the horizontal wire where the vertical one ends
    r = model::normalize_wires({{1, {0, 0}, {40, 0}}, {2, {20, 0}, {20, 30}}}, {}, next);
    CHECK(r.size() == 3);
    CHECK(has_wire(r, {0, 0}, {20, 0}));
    CHECK(has_wire(r, {20, 0}, {40, 0}));
    CHECK(has_wire(r, {20, 0}, {20, 30}));

    // crossing wires without a shared endpoint stay unsplit
    r = model::normalize_wires({{1, {0, 0}, {40, 0}}, {2, {20, -20}, {20, 20}}}, {}, next);
    CHECK(r.size() == 2);

    // extra points (pins) split wires, zero length wires are dropped
    r = model::normalize_wires({{1, {0, 0}, {40, 0}}, {2, {5, 5}, {5, 5}}}, {{10, 0}}, next);
    CHECK(r.size() == 2);
    CHECK(has_wire(r, {0, 0}, {10, 0}));
}

static void test_transform()
{
    model::transform t;
    t.rotation = 1;
    CHECK(t.apply(glm::ivec2{10, 0}) == glm::ivec2(0, 10));
    t.mirror = true;
    CHECK(t.apply(glm::ivec2{10, 0}) == glm::ivec2(0, -10));
}

static void test_ports_and_check()
{
    model::cell c;
    auto a_id = c.add_port(model::direction::input, {0, 0}).id;
    auto b_id = c.add_port(model::direction::input, {0, 20}).id;
    CHECK(c.find_port(a_id)->name == "in1");
    CHECK(c.find_port(b_id)->name == "in2");

    // ports without pins are reported
    CHECK(c.check().size() == 2);

    c.symbol.pins.push_back({c.new_id(), a_id, {-20, 0}});
    c.symbol.pins.push_back({c.new_id(), b_id, {-20, 10}});
    CHECK(c.check().empty());

    // removing a port removes its pin
    c.remove_port(a_id);
    CHECK(c.ports.size() == 1);
    CHECK(c.symbol.pins.size() == 1);
}

static void test_generate_symbol()
{
    model::cell c;
    c.name = "adder";
    c.add_port(model::direction::input, {0, 0});
    c.add_port(model::direction::input, {0, -20});
    c.add_port(model::direction::output, {50, 0});
    c.generate_symbol();

    CHECK(c.check().empty());
    CHECK(c.symbol.pins.size() == 3);

    auto b = c.symbol.bounds();
    for (auto& p : c.symbol.pins) {
        CHECK(p.pos.x % model::grid == 0 && p.pos.y % model::grid == 0);
        // strictly between the top and bottom edge of the box
        CHECK(p.pos.y > b.min.y && p.pos.y < b.max.y);
    }

    // inputs left of the origin, outputs right
    for (auto& p : c.symbol.pins)
        CHECK((c.find_port(p.port)->dir == model::direction::input) == (p.pos.x < 0));
}

static void test_cycles()
{
    model::workspace ws;
    auto& lib = ws.add_library("work");
    auto& a = ws.create_cell(lib, "a");
    auto& b = ws.create_cell(lib, "b");

    // b contains a
    b.get_schematic()->instances.push_back({b.new_id(), a.id, "", {0, 0}, {}});

    CHECK(ws.would_create_cycle(a.id, a.id));
    CHECK(ws.would_create_cycle(a.id, b.id));
    CHECK(!ws.would_create_cycle(b.id, a.id));
    CHECK(ws.users_of(a.id).size() == 1);

    std::string err;
    CHECK(!model::workspace::valid_cell_name(lib, "a", err));
    CHECK(!model::workspace::valid_cell_name(lib, "x/y", err));
    CHECK(model::workspace::valid_cell_name(lib, "half adder", err));
}

static void test_save_load(const std::filesystem::path& dir)
{
    std::filesystem::remove_all(dir);

    model::workspace ws;
    auto& lib = ws.add_library("work");
    auto& a = ws.create_cell(lib, "and2");
    auto& p = a.add_port(model::direction::output, {50, 0});
    p.width = 8;
    a.symbol.shapes.push_back({a.new_id(), model::line{{0, 0}, {10, 0}}});
    a.symbol.shapes.push_back({a.new_id(), model::bezier{{0, 0}, {10, 0}, {5, 5}}});
    a.symbol.shapes.push_back({a.new_id(), model::circle{{0, 0}, 2.5f}});
    a.symbol.shapes.push_back({a.new_id(), model::text{{0, 0}, "&", 12.0f}});
    a.symbol.pins.push_back({a.new_id(), p.id, {20, 0}});

    auto& top = ws.create_cell(lib, "top");
    top.get_schematic()->wires.push_back({top.new_id(), {0, 0}, {10, 0}});
    model::instance inst{top.new_id(), a.id, "U1", {30, 40}, {1, true}};
    top.get_schematic()->instances.push_back(inst);

    io::save_library(lib, dir);
    CHECK(!lib.dirty);

    model::workspace loaded;
    auto errors = io::load_workspace(loaded, dir);
    CHECK(errors.empty());
    for (auto& e : errors)
        std::printf("  %s\n", e.c_str());

    auto l = loaded.find_library("work");
    CHECK(l && l->cells.size() == 2);
    if (!l) return;

    // a round trip produces the same json
    CHECK(io::to_json(*l->find_cell("and2")) == io::to_json(a));
    CHECK(io::to_json(*l->find_cell("top")) == io::to_json(top));

    auto t = l->find_cell("top");
    CHECK(t->get_schematic()->instances[0].cell == a.id);
    CHECK(t->get_schematic()->instances[0].xf.mirror);

    // renaming a cell removes the old file on save
    a.name = "and";
    io::save_library(lib, dir);
    CHECK(!std::filesystem::exists(dir / "work" / "cells" / "and2.json"));
    CHECK(std::filesystem::exists(dir / "work" / "cells" / "and.json"));

    // a broken file is reported, the rest still loads
    {
        std::ofstream f(dir / "work" / "cells" / "broken.json");
        f << "{ not json";
    }
    model::workspace partial;
    errors = io::load_workspace(partial, dir);
    CHECK(errors.size() == 1);
    CHECK(partial.find_library("work")->cells.size() == 2);

    std::filesystem::remove_all(dir);
}

static void test_history()
{
    model::cell c;
    c.impl = model::schematic{};
    model::history h(c);

    CHECK(!h.can_undo());
    CHECK(!h.commit(c));

    c.add_port(model::direction::input, {0, 0});
    CHECK(h.commit(c));
    auto in = c.ports[0].id;
    c.find_port(in)->name = "a";
    CHECK(h.commit(c));

    // uncommitted changes are undone too
    c.get_schematic()->wires.push_back({c.new_id(), {0, 0}, {10, 0}});

    CHECK(h.undo(c));
    CHECK(c.get_schematic()->wires.empty());
    CHECK(c.find_port(in)->name == "a");
    CHECK(h.undo(c));
    CHECK(c.find_port(in)->name != "a");
    CHECK(h.undo(c));
    CHECK(c.ports.empty());
    CHECK(!h.undo(c));

    CHECK(h.redo(c));
    CHECK(c.ports.size() == 1);
    CHECK(h.redo(c));
    CHECK(c.find_port(in)->name == "a");
    CHECK(h.redo(c));
    CHECK(c.get_schematic()->wires.size() == 1);
    CHECK(!h.redo(c));

    // a change after undo drops the redo steps
    CHECK(h.undo(c));
    c.find_port(in)->width = 8;
    CHECK(!h.redo(c));
    CHECK(!h.can_redo());
    CHECK(c.find_port(in)->width == 8);
    CHECK(h.undo(c));
    CHECK(c.find_port(in)->width == 1);

    // old steps are dropped beyond the limit
    model::cell d;
    model::history small(d, 2);
    for (int i = 0; i < 5; i++) {
        d.add_port(model::direction::input, {0, i * 10});
        small.commit(d);
    }
    CHECK(small.undo(d) && small.undo(d));
    CHECK(!small.undo(d));
    CHECK(d.ports.size() == 3);
}

int main(int argc, char** argv)
{
    auto dir = std::filesystem::path(argc > 1 ? argv[1] : ".") / "model_test_workspace";

    test_normalize_wires();
    test_transform();
    test_ports_and_check();
    test_generate_symbol();
    test_cycles();
    test_history();
    test_save_load(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
