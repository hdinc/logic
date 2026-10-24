// tests for the model and io layers. no test framework, a failing check
// prints the location and the test exits with 1.

#include "../src/builtin/builtin.h"
#include "../src/io/json.h"
#include "../src/io/storage.h"
#include "../src/model/history.h"
#include "../src/model/workspace.h"
#include "../src/netlist/netlist.h"

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

static void test_default_instance_names()
{
    CHECK(model::is_default_instance_name("U1") && model::is_default_instance_name("U123"));
    CHECK(!model::is_default_instance_name("U") && !model::is_default_instance_name("U1a") && !model::is_default_instance_name("cpu"));
    CHECK(!model::is_default_instance_name("u1") && !model::is_default_instance_name(""));
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

    // the automatic symbol has all pins
    CHECK(c.auto_symbol() && c.check().empty());

    // in a symbol of its own, ports without pins are reported
    c.symbol.pins.push_back({c.new_id(), a_id, {-20, 0}});
    CHECK(!c.auto_symbol() && c.check().size() == 1);
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

    // pins of a side one grid apart, from the top
    std::vector<int> ys;
    for (auto& p : c.symbol.pins)
        if (p.pos.x < 0) ys.push_back(p.pos.y);
    CHECK(ys.size() == 2 && ys[0] - ys[1] == model::grid);
}

static void test_auto_symbol()
{
    model::workspace ws;
    auto& lib = ws.add_library("work");
    auto& c = ws.create_cell(lib, "half_adder");
    c.add_port(model::direction::input, {0, 0});
    c.add_port(model::direction::input, {0, -20});
    auto out = c.add_port(model::direction::output, {50, 0}).id;
    model::instance i{1, c.id, "U1", {0, 0}, {}, {}};

    // a box with a pin per port, not stored in the cell
    CHECK(c.auto_symbol());
    auto v = ws.interface_of(i);
    CHECK(v.symbol != &c.symbol && v.symbol->pins.size() == 3);
    CHECK(c.symbol.shapes.empty() && c.check().empty());
    for (auto& p : c.ports)
        CHECK(std::count_if(v.symbol->pins.begin(), v.symbol->pins.end(), [&](auto& pin) { return pin.port == p.id; }) == 1);

    // it follows the ports
    c.add_port(model::direction::output, {50, -20});
    CHECK(ws.interface_of(i).symbol->pins.size() == 4);
    c.remove_port(out);
    CHECK(ws.interface_of(i).symbol->pins.size() == 3);

    // a symbol of its own is used and stays, clearing it is automatic again
    c.generate_symbol();
    CHECK(!c.auto_symbol() && ws.interface_of(i).symbol == &c.symbol);
    c.add_port(model::direction::input, {0, -40});
    CHECK(ws.interface_of(i).symbol->pins.size() == 3);
    c.symbol = {};
    CHECK(c.auto_symbol() && ws.interface_of(i).symbol->pins.size() == 4);

    // only schematic cells
    model::cell ext;
    ext.impl = model::c_model_impl{};
    CHECK(!ext.auto_symbol());
}

static void test_transform_then()
{
    // turning or mirroring a placed thing as a whole: its pins end up where
    // the same change takes their positions
    glm::ivec2 pos{30, -20}, pivot{10, 40}, pin{20, 10};
    for (int r = 0; r < 4; r++) {
        for (bool m : {false, true}) {
            for (int turns : {0, 1, -1, 2}) {
                for (bool mirror : {false, true}) {
                    model::transform xf{r, m};
                    model::transform change{turns, mirror};
                    auto before = pos + xf.apply(pin);
                    auto want = pivot + change.apply(before - pivot);
                    auto moved = pivot + change.apply(pos - pivot);
                    auto got = moved + xf.then(turns, mirror).apply(pin);
                    CHECK(got == want);
                }
            }
        }
    }
}

static void test_cycles()
{
    model::workspace ws;
    auto& lib = ws.add_library("work");
    auto& a = ws.create_cell(lib, "a");
    auto& b = ws.create_cell(lib, "b");

    // b contains a
    b.get_schematic()->instances.push_back({b.new_id(), a.id, "", {0, 0}, {}, {}});

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
    p.description = "the result\nsecond line";
    a.symbol.shapes.push_back({a.new_id(), model::line{{0, 0}, {10, 0}}, model::bus_stroke});
    a.symbol.shapes.push_back({a.new_id(), model::bezier{{0, 0}, {10, 0}, {5, 5}}});
    a.symbol.shapes.push_back({a.new_id(), model::arc{{0, 10}, {0, -10}, {10, 0}}});
    a.symbol.shapes.push_back({a.new_id(), model::circle{{0, 0}, 2.5f}});
    a.symbol.shapes.push_back({a.new_id(), model::text{{0, 0}, "&", 12.0f}});
    a.symbol.pins.push_back({a.new_id(), p.id, {20, 0}});

    auto& top = ws.create_cell(lib, "top");
    top.get_schematic()->wires.push_back({top.new_id(), {0, 0}, {10, 0}});
    model::instance inst{top.new_id(), a.id, "U1", {30, 40}, {1, true}, {{"inputs", int64_t(3)}, {"on", true}, {"edge", std::string("falling")}}};
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
    CHECK(t->get_schematic()->instances[0].params == inst.params);

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

static void test_clipboard()
{
    model::cell part;
    part.impl = model::schematic{};
    part.add_port(model::direction::input, {-20, 0}).description = "a";
    model::instance i;
    i.id = 5;
    i.cell = model::builtin_cell_id("and");
    i.name = "U1";
    i.params["inputs"] = int64_t(3);
    part.get_schematic()->instances.push_back(i);
    part.get_schematic()->wires.push_back({6, {0, 0}, {10, 0}});

    auto text = io::to_clipboard(part, "schematic");
    auto back = io::from_clipboard(text, "schematic");
    CHECK(back.has_value());
    if (back) {
        CHECK(back->ports == part.ports);
        CHECK(*back->get_schematic() == *part.get_schematic());
    }

    // wrong kind or not ours
    CHECK(!io::from_clipboard(text, "symbol"));
    CHECK(!io::from_clipboard("hello", "schematic"));
    CHECK(!io::from_clipboard("{\"logic_clipboard\": \"schematic\"}", "schematic"));
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

static void test_params()
{
    std::vector<model::param_def> defs = {
        model::int_param("width", "Width", 1, 1, 64),
        model::bool_param("flag", "Flag", false),
        model::choice_param("edge", "Edge", {"rising", "falling"}, "rising"),
    };

    // defaults, clamping, wrong types and unknown names
    auto r = model::resolve_params(defs, {{"width", int64_t(100)}, {"flag", int64_t(1)}, {"edge", std::string("both")}, {"x", true}});
    CHECK(model::get_int(r, "width") == 64);
    CHECK(model::get_bool(r, "flag") == false);
    CHECK(model::get_string(r, "edge") == "rising");
    CHECK(r.size() == 3);

    // only values that differ from the default are stored
    model::param_values v;
    model::set_param(v, defs[0], int64_t(8));
    model::set_param(v, defs[2], std::string("rising"));
    CHECK(v.size() == 1 && model::get_int(v, "width") == 8);
    model::set_param(v, defs[0], int64_t(1));
    CHECK(v.empty());

    CHECK(model::params_key({{"a", std::string("1")}}) != model::params_key({{"a", int64_t(1)}}));
}

static void test_arc()
{
    // half circle on the right of the origin, both ways round
    for (auto through : {glm::ivec2{10, 0}, glm::ivec2{-10, 0}}) {
        auto pts = model::arc_points({{0, 10}, {0, -10}, through});
        CHECK(pts.size() > 4);
        CHECK(pts.front() == glm::vec2(0, 10) && pts.back() == glm::vec2(0, -10));
        for (auto p : pts) {
            CHECK(std::abs(glm::length(p) - 10.0f) < 1e-3f);
            CHECK(p.x * through.x >= -1e-3f);
        }
    }
    // on one line: just the ends
    CHECK(model::arc_points({{0, 0}, {10, 0}, {5, 0}}).size() == 2);
}

static bool on_grid(glm::ivec2 p) { return p.x % model::grid == 0 && p.y % model::grid == 0; }

static void test_builtins()
{
    model::workspace ws;
    auto& lib = ws.add_builtin_library(builtin::registry());
    CHECK(lib.read_only);

    auto and_id = model::builtin_cell_id("and");
    CHECK(and_id == model::builtin_cell_id("and"));
    CHECK(and_id != model::builtin_cell_id("or"));
    auto and_cell = ws.find_cell(and_id);
    CHECK(and_cell && and_cell->name == "AND");

    // every primitive has a component bar icon: a body without pins in its
    // 24 by 24 box
    for (auto& p : builtin::registry().all()) {
        auto icon = p->icon();
        CHECK(!icon.shapes.empty() && icon.pins.empty());
        auto b = icon.bounds();
        if (b.min.x < -12 || b.min.y < -12 || b.max.x > 12 || b.max.y > 12) std::printf("  icon of %s is outside its box\n", p->kind().c_str());
        CHECK(b.min.x >= -12 && b.min.y >= -12 && b.max.x <= 12 && b.max.y <= 12);
    }

    // every primitive builds valid interfaces for its defaults and extremes
    for (auto& p : builtin::registry().all()) {
        auto c = ws.find_cell(model::builtin_cell_id(p->kind()));
        CHECK(c && ws.primitive_of(*c) == p.get());

        for (int extreme = 0; extreme < 3; extreme++) {
            model::param_values v;
            for (auto& d : p->params())
                if (d.type == model::param_def::integer) v[d.name] = extreme == 0 ? d.def : (extreme == 1 ? model::param_value(d.min) : model::param_value(d.max));
            auto iface = p->build(model::resolve_params(p->params(), v));

            CHECK(!iface.ports.empty());
            CHECK(iface.symbol.pins.size() == iface.ports.size());
            for (auto& pin : iface.symbol.pins) {
                CHECK(on_grid(pin.pos));
                CHECK(std::any_of(iface.ports.begin(), iface.ports.end(), [&](auto& port) { return port.id == pin.port; }));
            }
            // linked bits exist
            for (auto& l : iface.links) {
                CHECK(l.a >= 0 && l.a < (int)iface.ports.size() && l.b >= 0 && l.b < (int)iface.ports.size());
                CHECK(l.width > 0 && l.a_bit >= 0 && l.b_bit >= 0);
                CHECK(l.a_bit + l.width <= iface.ports[l.a].width && l.b_bit + l.width <= iface.ports[l.b].width);
            }
            // no two pins at the same place
            for (size_t a = 0; a < iface.symbol.pins.size(); a++)
                for (size_t b = a + 1; b < iface.symbol.pins.size(); b++)
                    CHECK(iface.symbol.pins[a].pos != iface.symbol.pins[b].pos);
        }
    }

    // instances get the interface of their parameters, cached
    model::instance i;
    i.cell = and_id;
    i.params["inputs"] = int64_t(5);
    i.params["width"] = int64_t(8);
    auto iface = ws.interface_of(i);
    CHECK(iface && iface.ports->size() == 6);
    CHECK(std::all_of(iface.ports->begin(), iface.ports->end(), [](auto& p) { return p.width == 8; }));
    CHECK(ws.interface_of(i).symbol == iface.symbol);

    i.params.clear();
    CHECK(ws.interface_of(i).ports->size() == 3);

    // user cells use their own symbol (an empty one is automatic, see
    // test_auto_symbol), missing cells have none
    auto& work = ws.add_library("work");
    auto& user = ws.create_cell(work, "user");
    user.add_port(model::direction::input, {0, 0});
    user.generate_symbol();
    i.cell = user.id;
    CHECK(ws.interface_of(i).symbol == &user.symbol);
    CHECK(ws.params_of(i).empty());
    i.cell = 12345;
    CHECK(!ws.interface_of(i));
}


// an hdl cell with a WIDTH parameter: d and q are WIDTH bits, clk is 1
static model::cell& make_hdl_cell(model::workspace& ws, model::library& lib)
{
    model::hdl_impl h;
    h.language = model::hdl_language::systemverilog;
    h.backend = model::hdl_backend::icarus;
    h.sources = {"rtl/reg.sv", "rtl/pkg.sv"};
    h.top = "reg_n";
    h.include_dirs = {"rtl/include"};
    h.defines = {"SIM", "DEPTH=4"};
    h.args = "-Wall";
    h.params = {model::int_param("WIDTH", "WIDTH", 8, 1, 64, "data width"), model::choice_param("MODE", "MODE", {"a", "b"}, "a"),
        model::bool_param("FAST", "FAST", false), model::text_param("NAME", "NAME", "x")};
    auto& c = ws.create_cell(lib, "reg_n", std::move(h));
    c.add_port(model::direction::input, {0, 0}).name = "clk";
    auto& d = c.add_port(model::direction::input, {0, 0});
    d.name = "d";
    d.width = 8;
    auto& q = c.add_port(model::direction::output, {0, 0});
    q.name = "q";
    q.width = 8;
    c.generate_symbol(model::pin_order::declaration);
    return c;
}

// pretends to read reg_n: d and q are WIDTH bits, WIDTH 13 adds a port
class fake_provider : public model::external_provider {
public:
    mutable int calls = 0;

    bool handles(const model::cell& c) const override { return std::holds_alternative<model::hdl_impl>(c.impl); }

    bool describe(const model::cell& c, const std::filesystem::path&, std::vector<model::port>& ports, std::vector<model::param_def>& params,
        std::string& error) const override
    {
        if (std::get<model::hdl_impl>(c.impl).top != "reg_n") {
            error = "no top";
            return false;
        }
        params = {model::int_param("WIDTH", "WIDTH", 8, 1, 64)};
        return ports_for(c, {}, {{"WIDTH", int64_t(8)}}, ports, error);
    }

    bool ports_for(const model::cell&, const std::filesystem::path&, const model::param_values& values, std::vector<model::port>& ports,
        std::string& error) const override
    {
        calls++;
        int w = (int)model::get_int(values, "WIDTH");
        if (w == 7) {
            error = "seven is not allowed";
            return false;
        }
        ports.clear();
        model::port p;
        p.name = "clk";
        ports.push_back(p);
        p.name = "d";
        p.width = w;
        ports.push_back(p);
        p.name = "q";
        p.dir = model::direction::output;
        ports.push_back(p);
        if (w == 13) {
            p.name = "extra";
            p.width = 1;
            ports.push_back(p);
        }
        return true;
    }
};

static void test_external_cells(const std::filesystem::path& dir)
{
    std::filesystem::remove_all(dir);

    model::workspace ws;
    auto& lib = ws.add_library("work");
    auto& h = make_hdl_cell(ws, lib);
    CHECK(h.is_external() && !h.get_schematic());
    CHECK(h.external_params() && h.external_params()->size() == 4);
    CHECK(h.check().empty());

    // declaration order, buses with thick stubs
    CHECK(h.symbol.pins.size() == 3);
    auto pin_y = [&](const std::string& name) {
        for (auto& p : h.ports)
            if (p.name == name) return h.symbol.find_pin_of_port(p.id)->pos.y;
        return -999;
    };
    CHECK(pin_y("clk") > pin_y("d"));
    CHECK(std::any_of(h.symbol.shapes.begin(), h.symbol.shapes.end(), [](auto& s) { return s.width == model::bus_stroke; }));

    model::c_model_impl m;
    m.plugin = "chips";
    m.model = "m6502";
    m.params = {model::int_param("clock", "Clock", 1000000, 1, 100000000), model::file_param("firmware", "Firmware")};
    auto& cm = ws.create_cell(lib, "cpu", m);
    cm.add_port(model::direction::output, {0, 0}).width = 16;
    cm.add_port(model::direction::inout, {0, 0}).width = 8;
    cm.generate_symbol(model::pin_order::declaration);

    // a cell from a newer version is kept as it is
    auto future = io::to_json(cm);
    future["name"] = "future";
    future["id"] = io::id_to_string(model::workspace::random_id());
    future["impl"] = {{"type", "spice"}, {"netlist", "R1 a b 1k"}, {"nested", {{"x", 1}}}};

    io::save_library(lib, dir);
    {
        std::ofstream f(dir / "work" / "cells" / "future.json");
        f << future.dump(2);
    }
    CHECK(lib.dir == dir / "work");

    model::workspace loaded;
    auto errors = io::load_workspace(loaded, dir);
    for (auto& e : errors)
        std::printf("  %s\n", e.c_str());
    CHECK(errors.empty());
    auto l = loaded.find_library("work");
    CHECK(l && l->cells.size() == 3);
    if (!l) return;
    CHECK(l->dir == dir / "work");
    CHECK(l->find_cell("reg_n")->impl == h.impl);
    CHECK(l->find_cell("cpu")->impl == cm.impl);
    CHECK(io::to_json(*l->find_cell("reg_n")) == io::to_json(h));
    auto f = l->find_cell("future");
    CHECK(f && std::holds_alternative<model::unknown_impl>(f->impl));
    if (f) {
        CHECK(io::to_json(*f) == future);
        CHECK(f->ports.size() == 2);
    }
    std::filesystem::remove_all(dir);

    // instances: defaults use the cell's own interface
    model::instance i;
    i.cell = h.id;
    auto own = ws.interface_of(i);
    CHECK(own.ports == &h.ports && own.symbol == &h.symbol);
    CHECK(ws.params_of(i).size() == 4);
    CHECK(ws.check_instance(i).empty());

    // other parameters without a provider: the cell's interface, with a hint
    i.params = {{"WIDTH", int64_t(16)}};
    auto v = ws.interface_of(i);
    CHECK(v && (*v.ports)[1].width == 8);
    CHECK(!ws.check_instance(i).empty());

    fake_provider fp;
    ws.add_provider(fp);
    CHECK(ws.provider_of(h) == &fp);
    CHECK(!ws.provider_of(cm));

    // providers are added at startup, parameters not built before use it
    i.params = {{"WIDTH", int64_t(32)}};
    v = ws.interface_of(i);
    CHECK((*v.ports)[1].width == 32 && (*v.ports)[2].width == 32);
    // the same names keep the symbol and the ids
    CHECK(*v.symbol == h.symbol);
    CHECK((*v.ports)[1].id == h.ports[1].id);
    CHECK(ws.check_instance(i).empty());
    int calls = fp.calls;
    ws.interface_of(i);
    CHECK(fp.calls == calls);

    // a changed cell rebuilds
    h.ports[0].description = "clock";
    v = ws.interface_of(i);
    CHECK(fp.calls == calls + 1);
    CHECK((*v.ports)[0].description == "clock");

    // another port: a generated symbol
    i.params = {{"WIDTH", int64_t(13)}};
    v = ws.interface_of(i);
    CHECK(v.ports->size() == 4 && v.symbol->pins.size() == 4);

    // the provider fails: the cell's interface and the error
    i.params = {{"WIDTH", int64_t(7)}};
    v = ws.interface_of(i);
    CHECK((*v.ports)[1].width == 8);
    CHECK(ws.check_instance(i).find("seven") != std::string::npos);

    // refreshing replaces ports and params, a custom symbol stays while the
    // names do
    h.symbol.shapes.push_back({h.new_id(), model::circle{{0, 0}, 3.0f}});
    auto custom = h.symbol;
    auto d_id = h.ports[1].id;
    std::string error;
    CHECK(ws.refresh_interface(h, error));
    CHECK(h.external_params()->size() == 1);
    CHECK(h.symbol == custom);
    CHECK(h.ports[1].id == d_id && h.ports[0].description == "clock");

    std::get<model::hdl_impl>(h.impl).top = "other";
    CHECK(!ws.refresh_interface(h, error));
    CHECK(error == "no top");
    CHECK(!ws.refresh_interface(cm, error));

    // set_ports with a removed port regenerates the symbol
    auto ports = h.ports;
    ports.pop_back();
    CHECK(h.set_ports(ports));
    CHECK(h.symbol != custom && h.symbol.pins.size() == 2 && h.check().empty());
}

// a field of a nested bundle
static model::bundle_field sub_field(std::string name, model::id bundle, bool flipped = false)
{
    model::bundle_field f;
    f.name = std::move(name);
    f.bundle = bundle;
    f.flipped = flipped;
    return f;
}

// a handshake (valid and data out, ready in) and a bus with two of them, one
// flipped, and an input
struct bundle_fixture {
    model::workspace ws;
    model::library* lib;
    model::bundle_type* hs;
    model::bundle_type* bus;

    bundle_fixture()
    {
        lib = &ws.add_library("work");
        hs = &ws.create_bundle(*lib, "hs");
        hs->fields = {{"valid", "", 1, model::direction::output}, {"ready", "", 1, model::direction::input}, {"data", "", 8, model::direction::output}};
        bus = &ws.create_bundle(*lib, "bus");
        bus->fields = {sub_field("a", hs->id), sub_field("b", hs->id, true), {"irq", "", 1, model::direction::input}};
    }
};

static void test_bundles(const std::filesystem::path& dir)
{
    using model::direction;
    bundle_fixture f;
    auto& ws = f.ws;

    // fields take the bits in order, nested flips combine
    auto l = ws.layout_of(f.bus->id);
    CHECK(l && l.width == 21);
    CHECK(l.leaves.size() == 7);
    CHECK((l.leaves[0] == model::bundle_leaf{"a.valid", direction::output, 0, 1}));
    CHECK((l.leaves[1] == model::bundle_leaf{"a.ready", direction::input, 1, 1}));
    CHECK((l.leaves[2] == model::bundle_leaf{"a.data", direction::output, 2, 8}));
    CHECK((l.leaves[3] == model::bundle_leaf{"b.valid", direction::input, 10, 1}));
    CHECK((l.leaves[4] == model::bundle_leaf{"b.ready", direction::output, 11, 1}));
    CHECK((l.leaves[6] == model::bundle_leaf{"irq", direction::input, 20, 1}));
    // nodes: a, its 3 fields, b, its 3 fields, irq
    CHECK(l.nodes.size() == 9);
    auto b = l.find("b");
    CHECK(b && b->bundle == f.hs->id && b->flipped && b->offset == 10 && b->width == 10 && b->depth == 0);
    CHECK(l.find("b.data") && l.find("b.data")->depth == 1);

    // the flipped view swaps every direction, twice flipped is normal again
    auto fl = ws.layout_of(f.bus->id, true);
    CHECK(fl.leaves[0].dir == direction::input && fl.leaves[3].dir == direction::output && fl.leaves[6].dir == direction::output);
    CHECK(fl.find("b") && !fl.find("b")->flipped);
    CHECK(ws.bundle_label(f.bus->id, true) == "~bus");

    // a bundle can't contain itself, also not through another one
    CHECK(ws.would_create_bundle_cycle(f.hs->id, f.hs->id));
    CHECK(ws.would_create_bundle_cycle(f.hs->id, f.bus->id));
    CHECK(!ws.would_create_bundle_cycle(f.bus->id, f.hs->id));
    f.hs->fields.push_back(sub_field("loop", f.bus->id));
    CHECK(!ws.layout_of(f.bus->id) && ws.layout_of(f.bus->id).leaves.empty());
    f.hs->fields.pop_back();
    CHECK(!ws.layout_of(12345));

    std::string err;
    CHECK(!model::workspace::valid_bundle_name(*f.lib, "hs", err));
    CHECK(model::workspace::valid_bundle_name(*f.lib, "axi lite", err));

    // ports of a bundle type follow its width
    auto& sub = ws.create_cell(*f.lib, "sub");
    auto& m = sub.add_port(direction::output, {0, 0});
    m.name = "m";
    m.bundle = f.bus->id;
    auto m_id = m.id;
    sub.symbol.pins.push_back({sub.new_id(), m_id, {0, 0}});
    ws.bundles_changed();
    CHECK(sub.find_port(m_id)->width == 21);
    f.hs->fields[2].width = 4;
    auto rev = ws.bundle_revision();
    ws.bundles_changed();
    CHECK(ws.bundle_revision() == rev + 1);
    CHECK(sub.find_port(m_id)->width == 13);
    f.hs->fields[2].width = 8;
    ws.bundles_changed();

    // a bundle pin connected to the same bundle is fine, to plain bits or
    // another bundle of the same width a type error
    auto& top = ws.create_cell(*f.lib, "top");
    auto& in = top.add_port(direction::input, {80, 0});
    in.width = 21;
    in.bundle = f.bus->id;
    in.flipped = true;
    top.get_schematic()->instances.push_back({top.new_id(), sub.id, "U1", {100, 0}, {}, {}});
    auto nl = netlist::extract(ws, top);
    CHECK(nl.problems.empty());
    CHECK(nl.nets.size() == 1 && nl.nets[0].bundle == f.bus->id && !nl.nets[0].type_error);

    top.ports[0].bundle = model::no_id;
    nl = netlist::extract(ws, top);
    CHECK(nl.nets.size() == 1 && nl.nets[0].type_error && !nl.nets[0].width_error);
    CHECK(nl.problems.size() == 1 && nl.problems[0].find("type mismatch") != std::string::npos);

    // a missing type is reported, the port keeps its width
    top.ports[0].bundle = 12345;
    ws.bundles_changed();
    CHECK(top.ports[0].width == 21);
    nl = netlist::extract(ws, top);
    CHECK(nl.problems.size() == 2);
    top.ports[0].bundle = f.bus->id;

    // an hdl cell grouping its ports into a bundle port
    model::hdl_impl h;
    h.top = "m";
    auto& hc = ws.create_cell(*f.lib, "hdl", h);
    for (auto [name, dir, w] : {std::tuple{"s_valid", direction::input, 1}, {"s_ready", direction::output, 1}, {"s_data", direction::input, 8}}) {
        auto& p = hc.add_port(dir, {0, 0});
        p.name = name;
        p.width = w;
    }
    model::port_group g;
    g.name = "s";
    g.bundle = f.hs->id;
    g.flipped = true;
    hc.port_groups()->push_back(g);
    CHECK(ws.apply_groups(hc).empty());
    CHECK(hc.ports.size() == 1 && hc.model_ports().size() == 3);
    CHECK(model::field_port_name(g, "aw.valid") == "s_aw_valid");

    // bundles and bundle ports are saved and loaded
    std::filesystem::remove_all(dir);
    io::save_library(*f.lib, dir);
    CHECK(std::filesystem::exists(dir / "work" / "bundles" / "hs.json"));
    model::workspace loaded;
    auto errors = io::load_workspace(loaded, dir);
    CHECK(errors.empty());
    auto ll = loaded.find_library("work");
    CHECK(ll && ll->bundles.size() == 2 && !ll->dirty);
    if (ll) {
        CHECK(*ll->find_bundle("bus") == *f.bus);
        CHECK(*ll->find_bundle("hs") == *f.hs);
        CHECK(io::to_json(*ll->find_cell("top")) == io::to_json(top));
        CHECK(ll->find_cell("top")->ports[0].flipped);
        CHECK(ll->find_cell("hdl")->impl == hc.impl);
        CHECK(ll->find_cell("hdl")->ports == hc.ports);
    }

    // a stored width from before the type changed is updated on loading:
    // the port is saved with 21 bits, with 2 bits of data both handshakes
    // are 4 bits
    f.hs->fields[2].width = 2;
    io::save_library(*f.lib, dir);
    model::workspace changed;
    io::load_workspace(changed, dir);
    CHECK(changed.find_library("work")->find_cell("sub")->ports[0].width == 9);
    f.hs->fields[2].width = 8;

    // a deleted bundle removes its file
    ws.delete_bundle(f.bus->id);
    CHECK(!ws.find_bundle(f.bus->id));
    io::save_library(*f.lib, dir);
    CHECK(!std::filesystem::exists(dir / "work" / "bundles" / "bus.json"));
    CHECK(std::filesystem::exists(dir / "work" / "bundles" / "hs.json"));
    std::filesystem::remove_all(dir);
}

// the ports of Murax (VexRiscv) as verilator finds them
static void test_bundles_from_prefixes()
{
    using model::direction;
    model::workspace ws;
    auto& lib = ws.add_library("work");
    model::hdl_impl h;
    h.top = "Murax";
    auto& c = ws.create_cell(lib, "murax", h);
    struct leaf {
        const char* name;
        direction dir;
        int width;
    };
    const leaf leaves[] = {
        {"io_asyncReset", direction::input, 1},
        {"io_mainClk", direction::input, 1},
        {"io_jtag_tms", direction::input, 1},
        {"io_jtag_tdi", direction::input, 1},
        {"io_jtag_tdo", direction::output, 1},
        {"io_jtag_tck", direction::input, 1},
        {"io_gpioA_read", direction::input, 32},
        {"io_gpioA_write", direction::output, 32},
        {"io_gpioA_writeEnable", direction::output, 32},
        {"io_uart_txd", direction::output, 1},
        {"io_uart_rxd", direction::input, 1},
    };
    for (auto [name, dir, w] : leaves) {
        auto& p = c.add_port(dir, {0, 0});
        p.name = name;
        p.width = w;
    }

    auto found = model::common_prefixes(c.ports);
    CHECK(found.size() == 4);
    CHECK(found[0].prefix == "io_" && found[0].ports == std::vector<int>({0, 1}));
    CHECK(found[1].prefix == "io_jtag_" && found[1].ports.size() == 4);
    CHECK(found[2].prefix == "io_gpioA_" && found[3].prefix == "io_uart_");

    // a type per prefix, the ports grouped in the model's (normal) view
    for (int k = 1; k < 4; k++)
        c.port_groups()->push_back(ws.bundle_from_ports(lib, c.ports, found[k]));
    CHECK(ws.apply_groups(c).empty());
    CHECK(c.ports.size() == 5 && c.model_ports().size() == 11);
    auto jtag = lib.find_bundle("jtag");
    CHECK(jtag && jtag->fields.size() == 4 && jtag->fields[2].name == "tdo" && jtag->fields[2].dir == direction::output);
    CHECK(c.ports[2].name == "io_jtag" && c.ports[2].bundle == jtag->id && !c.ports[2].flipped && c.ports[2].width == 4);
    // mostly inputs go left
    CHECK(c.ports[2].dir == direction::input && c.ports[3].dir == direction::output);
    auto gpio = lib.find_bundle("gpioA");
    CHECK(gpio && ws.layout_of(gpio->id).width == 96);

    // a second type of the same name gets a number
    auto again = ws.bundle_from_ports(lib, c.model_ports(), found[1]);
    CHECK(ws.find_bundle(again.bundle)->name == "jtag2");
}

// right side labels of generated symbols end at the box
static void test_label_alignment()
{
    model::cell c;
    c.id = 1;
    c.add_port(model::direction::input, {0, 0});
    c.add_port(model::direction::output, {0, 0}).name = "a_long_output";
    c.generate_symbol();
    int right = 0;
    for (auto& s : c.symbol.shapes) {
        auto t = std::get_if<model::text>(&s.geom);
        if (t && t->str == "a_long_output") {
            right++;
            CHECK(t->align == model::text_align::right);
        }
        if (t && t->str == "in1") CHECK(t->align == model::text_align::left);
    }
    CHECK(right == 1);
    auto j = io::to_json(c);
    CHECK(io::to_json(io::cell_from_json(j)) == j);
    CHECK(io::cell_from_json(j).symbol == c.symbol);

    // a port's number format is saved when it isn't hex
    c.ports[1].format = {model::number_format::signed_fixed, 4};
    j = io::to_json(c);
    CHECK(io::cell_from_json(j).ports[1].format == c.ports[1].format);
    CHECK(!j["ports"][0].contains("format"));
    j["ports"][1]["format"]["kind"] = "from_the_future";
    CHECK(io::cell_from_json(j).ports[1].format.kind == model::number_format::hex);
}

// categories of cells: saved, listed by the library, builtins have theirs
static void test_categories(const std::filesystem::path& dir)
{
    model::workspace ws;
    auto& builtins = ws.add_builtin_library(builtin::registry());
    CHECK(ws.find_cell(model::builtin_cell_id("addsub"))->category == "Arithmetic");
    CHECK(ws.find_cell(model::builtin_cell_id("mux"))->category == "Plexers");
    auto cats = builtins.categories();
    CHECK(std::find(cats.begin(), cats.end(), "Logic gates") != cats.end() && std::is_sorted(cats.begin(), cats.end()));

    auto& lib = ws.add_library("work");
    ws.create_cell(lib, "alu").category = "datapath";
    ws.create_cell(lib, "regs").category = "datapath";
    ws.create_cell(lib, "top");
    CHECK(lib.categories() == std::vector<std::string>{"datapath"});

    std::filesystem::remove_all(dir);
    io::save_library(lib, dir);
    model::workspace loaded;
    io::load_workspace(loaded, dir);
    auto l = loaded.find_library("work");
    CHECK(l && l->find_cell("alu")->category == "datapath" && l->find_cell("top")->category.empty());
    // no category, no key in the file
    CHECK(!io::to_json(*lib.find_cell("top")).contains("category"));
    std::filesystem::remove_all(dir);
}

// polygon area, positive counter clockwise
static float area(const std::vector<glm::vec2>& ps)
{
    float a = 0;
    for (size_t k = 0; k < ps.size(); k++) {
        auto p = ps[k], q = ps[(k + 1) % ps.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a / 2;
}

static void test_fill_outlines()
{
    auto& reg = builtin::registry();
    auto symbol_of = [&](const char* kind, model::param_values v = {}) {
        auto p = reg.find(kind);
        return p->build(model::resolve_params(p->params(), v)).symbol;
    };

    // gates: one outline, the leads and the output line left out
    for (auto kind : {"and", "or", "xor", "nand"}) {
        auto o = model::fill_outlines(symbol_of(kind));
        // nand: the body and the bubble
        CHECK(o.size() == (std::string(kind) == "nand" ? 2u : 1u));
        if (!o.empty()) CHECK(std::abs(area(o.back())) > 100 || std::abs(area(o.front())) > 100);
    }
    // 9 inputs extend the back line of an and gate past its corners (to
    // +-40, the body is +-30): the outline is only the body
    auto and9 = model::fill_outlines(symbol_of("and", {{"inputs", int64_t(9)}}));
    CHECK(and9.size() == 1);
    float top = -1e9f;
    for (auto& o : and9)
        for (auto p : o)
            top = std::max(top, p.y);
    CHECK(std::abs(top - 30) < 0.01f);

    // a register: the box, the clock triangle on its edge doesn't count
    auto r = model::fill_outlines(symbol_of("register"));
    CHECK(r.size() == 1 && !r.empty() && std::abs(std::abs(area(r[0])) - 40 * 60) < 1);

    // a generated box with pin stubs on its edges
    model::cell c;
    c.add_port(model::direction::input, {0, 0});
    c.add_port(model::direction::output, {0, 0});
    c.generate_symbol();
    auto box = model::fill_outlines(c.symbol);
    CHECK(box.size() == 1);

    // a cell's own fill color is saved, "no fill" too
    c.id = 1;
    c.fill = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    auto filled = io::cell_from_json(io::to_json(c));
    CHECK(filled.fill && glm::distance(*filled.fill, *c.fill) < 0.01f);
    c.fill = glm::vec4(0.0f);
    CHECK(io::cell_from_json(io::to_json(c)).fill == glm::vec4(0.0f));
    c.fill.reset();
    CHECK(!io::to_json(c).contains("fill") && !io::cell_from_json(io::to_json(c)).fill);

    // an open polyline has nothing to fill
    model::symbol open;
    open.shapes.push_back({1, model::line{{0, 0}, {10, 0}}});
    open.shapes.push_back({2, model::line{{10, 0}, {10, 10}}});
    CHECK(model::fill_outlines(open).empty());
}

static void test_seg7_display()
{
    auto p = builtin::registry().find("seg7_display");
    CHECK(p);
    if (!p) return;

    // every input mode, bus side and option builds pins on the grid, apart
    for (const char* input : {"bus", "pins", "hex"})
        for (const char* side : {"left", "right", "top", "bottom"})
            for (int opts = 0; opts < 4; opts++) {
                model::param_values v{{"input", std::string(input)}, {"side", std::string(side)}, {"dp", (opts & 1) != 0}, {"enable", (opts & 2) != 0}};
                v = model::resolve_params(p->params(), v);
                CHECK(p->check(v).empty());
                auto iface = p->build(v);
                for (auto& pin : iface.symbol.pins)
                    CHECK(on_grid(pin.pos));
                for (size_t a = 0; a < iface.symbol.pins.size(); a++)
                    for (size_t b = a + 1; b < iface.symbol.pins.size(); b++)
                        CHECK(iface.symbol.pins[a].pos != iface.symbol.pins[b].pos);

                std::string mode = input;
                size_t expected = (mode == "pins" ? 7 : 1) + ((opts & 1) && mode != "bus" ? 1 : 0) + ((opts & 2) ? 1 : 0);
                CHECK(iface.ports.size() == expected);
                auto& first = iface.ports[0];
                if (mode == "bus") CHECK(first.name == "seg" && first.width == ((opts & 1) ? 8 : 7));
                if (mode == "hex") CHECK(first.name == "in" && first.width == 4);
                if (mode == "pins") CHECK(first.name == "a" && iface.ports[6].name == "g" && iface.ports[6].width == 1);
            }

    // the bus is on the left by default
    auto def = p->build(model::resolve_params(p->params(), {}));
    CHECK(def.ports.size() == 1 && def.symbol.pins[0].pos == glm::ivec2(-30, 0));

    CHECK(builtin::seg7_hex(0) == 0x3F && builtin::seg7_hex(8) == 0x7F && builtin::seg7_hex(10) == 0x77 && builtin::seg7_hex(15) == 0x71);
}

int main(int argc, char** argv)
{
    auto dir = std::filesystem::path(argc > 1 ? argv[1] : ".") / "model_test_workspace";

    test_normalize_wires();
    test_transform();
    test_default_instance_names();
    test_ports_and_check();
    test_generate_symbol();
    test_auto_symbol();
    test_transform_then();
    test_cycles();
    test_history();
    test_params();
    test_arc();
    test_clipboard();
    test_builtins();
    test_seg7_display();
    test_save_load(dir);
    test_external_cells(dir);
    test_bundles(dir);
    test_bundles_from_prefixes();
    test_label_alignment();
    test_categories(dir);
    test_fill_outlines();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
