// tests for the netlist and the simulator with the builtin primitives. no test
// framework, a failing check prints the location and the test exits with 1.

#include "../src/builtin/builder.h"
#include "../src/builtin/builtin.h"
#include "../src/io/firmware.h"
#include "../src/sim/arith.h"
#include "../src/sim/numbers.h"
#include "../src/sim/runner.h"
#include "../src/sim/simulator.h"

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <thread>

using namespace logic;
using sim::value;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

// a workspace with the builtins and one schematic cell to fill
struct fixture {
    model::workspace ws;
    model::cell* top;

    explicit fixture(const model::primitive_registry& r = builtin::registry())
    {
        ws.add_builtin_library(r);
        top = &ws.create_cell(ws.add_library("work"), "top");
    }

    model::schematic& sch() { return *top->get_schematic(); }

    void place(const std::string& kind, glm::ivec2 pos, model::param_values params = {})
    {
        place_cell(model::builtin_cell_id(kind), pos, std::move(params));
    }

    void place_cell(model::id cell, glm::ivec2 pos, model::param_values params = {})
    {
        model::instance i;
        i.id = top->new_id();
        i.cell = cell;
        i.name = "U" + std::to_string(sch().instances.size() + 1);
        i.pos = pos;
        i.params = std::move(params);
        sch().instances.push_back(i);
    }

    // starts a new cell, the following calls fill it
    model::cell& new_cell(const std::string& name)
    {
        top = &ws.create_cell(*ws.find_library("work"), name);
        return *top;
    }

    // input ports connect at pos + (20, 0), outputs at pos - (20, 0)
    int port(model::direction dir, glm::ivec2 connect_at, int width = 1)
    {
        auto pos = connect_at + glm::ivec2{dir == model::direction::input ? -20 : 20, 0};
        top->add_port(dir, pos).width = width;
        return (int)top->ports.size() - 1;
    }

    void wire(glm::ivec2 a, glm::ivec2 b) { sch().wires.push_back({top->new_id(), a, b}); }
};

static value bit(bool b) { return sim::known(b, 1); }

static void test_values()
{
    CHECK(sim::to_string(sim::known(1, 1)) == "1");
    CHECK(sim::to_string(sim::known(0xAB, 8)) == "0xAB");
    CHECK(sim::to_string(value(2, 0b01, 0b10, 0)) == "x1");
    CHECK(sim::to_string(sim::floating(1)) == "z");

    // undriven bits stay z, disagreeing drivers give x
    value a(4, 0b0011, 0, 0b1100);
    value b(4, 0b0001, 0, 0b0100);
    auto r = sim::resolve(std::vector<value>{a, b}, 4);
    CHECK(r.bits() == 0b0001);
    CHECK(r.x() == 0b0010);
    CHECK(r.z() == 0b0100);

    CHECK(sim::resolve({}, 3) == sim::floating(3));
}

// and gate at the origin: inputs at (-30, 10) and (-30, -10), output (30, 0)
static void test_and_gate()
{
    fixture f;
    f.place("and", {0, 0});
    int a = f.port(model::direction::input, {-30, 10});
    // connected through a wire
    int b = f.port(model::direction::input, {-80, -10});
    f.wire({-80, -10}, {-30, -10});
    int y = f.port(model::direction::output, {30, 0});

    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());

    for (int i = 0; i < 4; i++) {
        s.set_input(a, bit(i & 1));
        s.set_input(b, bit(i & 2));
        CHECK(s.port_value(y) == bit(i == 3));
    }

    // the wire carries the value of b
    CHECK(s.net_value(s.net_of_wire(s.root(), f.sch().wires[0].id)) == bit(true));

    // x on one input: 0 & x is 0, 1 & x is x
    s.set_input(a, sim::unknown(1));
    s.set_input(b, bit(false));
    CHECK(s.port_value(y) == bit(false));
    s.set_input(b, bit(true));
    CHECK(s.port_value(y) == sim::unknown(1));
}

static void test_gates()
{
    struct expect {
        const char* kind;
        int results[4]; // for inputs 00, 01, 10, 11
    };
    expect table[] = {
        {"and", {0, 0, 0, 1}},
        {"or", {0, 1, 1, 1}},
        {"xor", {0, 1, 1, 0}},
        {"nand", {1, 1, 1, 0}},
        {"nor", {1, 0, 0, 0}},
        {"xnor", {1, 0, 0, 1}},
    };

    for (auto& e : table) {
        fixture f;
        f.place(e.kind, {0, 0});
        int a = f.port(model::direction::input, {-30, 10});
        int b = f.port(model::direction::input, {-30, -10});
        int y = f.port(model::direction::output, {30, 0});
        sim::simulator s(f.ws, *f.top, builtin::factory());

        for (int i = 0; i < 4; i++) {
            s.set_input(a, bit(i & 2));
            s.set_input(b, bit(i & 1));
            if (s.port_value(y) != bit(e.results[i])) std::printf("  %s with inputs %d%d\n", e.kind, (i >> 1) & 1, i & 1);
            CHECK(s.port_value(y) == bit(e.results[i]));
        }
    }

    // multi bit, unconnected inputs are ignored
    fixture f;
    f.place("or", {0, 0}, {{"inputs", int64_t(3)}, {"width", int64_t(8)}});
    // three inputs at y 10, 0, -10, the last one stays unconnected
    int a = f.port(model::direction::input, {-30, 10}, 8);
    int b = f.port(model::direction::input, {-30, 0}, 8);
    int y = f.port(model::direction::output, {30, 0}, 8);
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());
    s.set_input(a, sim::known(0xF0, 8));
    s.set_input(b, sim::known(0x0C, 8));
    CHECK(s.port_value(y) == sim::known(0xFC, 8));
}

static void test_not_chain()
{
    // not at (0, 0): in (-20, 0), out (20, 0). three in a row invert
    fixture f;
    f.place("not", {0, 0});
    f.place("not", {40, 0});
    f.place("not", {80, 0});
    int a = f.port(model::direction::input, {-20, 0});
    int y = f.port(model::direction::output, {100, 0});
    sim::simulator s(f.ws, *f.top, builtin::factory());

    s.set_input(a, bit(false));
    CHECK(s.port_value(y) == bit(true));
    s.set_input(a, bit(true));
    CHECK(s.port_value(y) == bit(false));
    CHECK(!s.oscillating());
}

static void test_oscillation()
{
    // a not gate feeding itself starts as x and stays x
    fixture f;
    f.place("not", {0, 0});
    f.wire({20, 0}, {20, 20});
    f.wire({20, 20}, {-20, 20});
    f.wire({-20, 20}, {-20, 0});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(!s.oscillating());
    CHECK(s.net_value(s.net_of_wire(s.root(), f.sch().wires[0].id)) == sim::unknown(1));

    // nand with its output on one input: 1 while the other input is 0,
    // never settles once it is 1
    fixture g;
    g.place("nand", {0, 0});
    int a = g.port(model::direction::input, {-30, 10});
    g.wire({30, 0}, {30, -30});
    g.wire({30, -30}, {-30, -30});
    g.wire({-30, -30}, {-30, -10});
    sim::simulator t(g.ws, *g.top, builtin::factory());
    CHECK(!t.oscillating());
    CHECK(t.net_value(t.net_of_wire(t.root(), g.sch().wires[0].id)) == bit(true));
    t.set_input(a, bit(true));
    CHECK(t.oscillating());
    t.set_input(a, bit(false));
    CHECK(!t.oscillating());
}

static void test_problems()
{
    // width mismatch: 8 bit gate input on a 1 bit port
    fixture f;
    f.place("and", {0, 0}, {{"width", int64_t(8)}});
    f.port(model::direction::input, {-30, 10});
    int y = f.port(model::direction::output, {30, 0}, 8);
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().size() == 1);
    CHECK(s.port_value(y) == sim::unknown(8));

    // missing cells are reported, an empty subcircuit is fine
    fixture g;
    auto& sub = g.ws.create_cell(*g.ws.find_library("work"), "sub");
    model::instance i;
    i.id = g.top->new_id();
    i.cell = sub.id;
    i.name = "S1";
    g.sch().instances.push_back(i);
    i.id = g.top->new_id();
    i.cell = 12345;
    i.name = "M1";
    g.sch().instances.push_back(i);
    sim::simulator t(g.ws, *g.top, builtin::factory());
    CHECK(t.problems().size() == 1);
    CHECK(t.root().children.size() == 1);

    // external cells can be placed but not simulated yet
    auto& ext = g.ws.create_cell(*g.ws.find_library("work"), "ext", model::c_model_impl{});
    i.id = g.top->new_id();
    i.cell = ext.id;
    i.name = "X1";
    g.sch().instances.push_back(i);
    sim::simulator u(g.ws, *g.top, builtin::factory());
    CHECK(u.problems().size() == 2);
    CHECK(u.problems().back().find("nothing can simulate") != std::string::npos);
}

static void test_conflict()
{
    // constants 1 and 0 driving the same net give x
    fixture f;
    f.place("constant", {0, 0});
    f.place("constant", {20, 20}, {{"value", int64_t(0)}});
    f.wire({10, 0}, {30, 0});
    f.wire({30, 0}, {30, 20});
    int y = f.port(model::direction::output, {30, 0});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.port_value(y) == sim::unknown(1));
}

static void test_register()
{
    // register at (100, 0): d (70, 10), clk (70, -10), q (130, 10).
    // clock at (60, -10) puts its output on the clk pin.
    fixture f;
    f.place("register", {100, 0});
    f.place("clock", {60, -10});
    int d = f.port(model::direction::input, {70, 10}, 8);
    int q = f.port(model::direction::output, {130, 10}, 8);
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());

    CHECK(s.port_value(q) == sim::known(0, 8));
    s.set_input(d, sim::known(0x42, 8));
    CHECK(s.port_value(q) == sim::known(0, 8));

    // rising edge at the first step
    s.step();
    CHECK(s.port_value(q) == sim::known(0x42, 8));
    s.set_input(d, sim::known(0x10, 8));
    s.step(); // falling
    CHECK(s.port_value(q) == sim::known(0x42, 8));
    s.step(); // rising
    CHECK(s.port_value(q) == sim::known(0x10, 8));
    // 1 Hz: edges every half second
    CHECK(s.now() == 3 * sim::seconds / 2);

    s.reset();
    CHECK(s.port_value(q) == sim::known(0, 8));
    CHECK(s.input_value(d) == sim::known(0x10, 8));
}

static void test_counter()
{
    // 1 bit toggle: register q -> not -> d. q flips on every rising edge.
    // register at (0, 0): d (-30, 10), clk (-30, -10), q (30, 10)
    fixture f;
    f.place("register", {0, 0}, {{"width", int64_t(1)}});
    f.place("clock", {-40, -10});
    // not from q around to d: in (50, 10)... placed mirrored is not needed,
    // the wire goes back
    f.place("not", {60, 10});
    f.wire({30, 10}, {40, 10});
    f.wire({80, 10}, {90, 10});
    f.wire({90, 10}, {90, 40});
    f.wire({90, 40}, {-30, 40});
    f.wire({-30, 40}, {-30, 10});
    int q = f.port(model::direction::output, {30, 10});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());

    bool expect = false;
    CHECK(s.port_value(q) == bit(expect));
    for (int i = 1; i <= 8; i++) {
        s.step();
        if (i % 2 == 1) expect = !expect;
        CHECK(s.port_value(q) == bit(expect));
    }
    CHECK(!s.oscillating());
}

// position of the port's pin for an instance of c placed at `at`
static glm::ivec2 pin_of(const model::cell& c, const std::string& port, glm::ivec2 at)
{
    for (auto& p : c.ports)
        if (p.name == port)
            if (auto pin = c.symbol.find_pin_of_port(p.id)) return at + pin->pos;
    std::printf("no pin for %s\n", port.c_str());
    failures++;
    return at;
}

// an and gate between the ports a, b and y, with a generated symbol

static model::cell& make_and_cell(fixture& f, const std::string& name)
{
    auto& c = f.new_cell(name);
    f.place("and", {0, 0});
    f.port(model::direction::input, {-30, 10});  // a
    f.port(model::direction::input, {-30, -10}); // b
    f.port(model::direction::output, {30, 0});   // y
    c.ports[0].name = "a";
    c.ports[1].name = "b";
    c.ports[2].name = "y";
    c.generate_symbol();
    return c;
}

static void test_hierarchy()
{
    fixture f;
    auto& foo = make_and_cell(f, "foo");

    // bar: y = (a & b) & c with two instances of foo, U1 feeds U2
    auto& bar = f.new_cell("bar");
    f.place_cell(foo.id, {0, 0});
    f.place_cell(foo.id, {200, 0});
    auto u1_y = pin_of(foo, "y", {0, 0});
    auto u2_a = pin_of(foo, "a", {200, 0});
    auto u2_b = pin_of(foo, "b", {200, 0});
    f.wire(u1_y, {u1_y.x, u2_a.y});
    f.wire({u1_y.x, u2_a.y}, u2_a);
    int a = f.port(model::direction::input, pin_of(foo, "a", {0, 0}));
    int b = f.port(model::direction::input, pin_of(foo, "b", {0, 0}));
    int c = f.port(model::direction::input, u2_b);
    int y = f.port(model::direction::output, pin_of(foo, "y", {200, 0}));

    sim::simulator s(f.ws, bar, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());
    CHECK(s.cells().size() == 2);

    for (int i = 0; i < 8; i++) {
        s.set_input(a, bit(i & 1));
        s.set_input(b, bit(i & 2));
        s.set_input(c, bit(i & 4));
        CHECK(s.port_value(y) == bit(i == 7));
    }

    // every instance has its own scope with its own values
    auto& root = s.root();
    CHECK(root.children.size() == 2);
    auto u1 = root.child(0);
    auto u2 = root.child(1);
    CHECK(u1 && u2 && u1->cell == &foo && u2->cell == &foo);
    CHECK(u1->path == "U1" && u2->path == "U2");
    CHECK(u1->nets == u2->nets);

    s.set_input(a, bit(true));
    s.set_input(b, bit(true));
    s.set_input(c, bit(false));
    // U1: a=1 b=1 y=1, U2: a=1 b=0 y=0. foo's ports are 0 a, 1 b, 2 y
    CHECK(s.port_value(*u1, 2) == bit(true));
    CHECK(s.port_value(*u2, 1) == bit(false));
    CHECK(s.port_value(*u2, 2) == bit(false));
    // the net at U1's output inside foo is the net at U2's input in bar
    CHECK(s.net_of_port(*u1, 2) == s.net_of_port(*u2, 0));
}

static void test_nested_state()
{
    // toggle: a register and a not gate as a cell, used twice. both keep
    // their own state, one gets a clock and one doesn't.
    fixture f;
    auto& t = f.new_cell("toggle");
    f.place("register", {0, 0}, {{"width", int64_t(1)}});
    f.place("not", {60, 10});
    f.wire({30, 10}, {40, 10});
    f.wire({80, 10}, {90, 10});
    f.wire({90, 10}, {90, 40});
    f.wire({90, 40}, {-30, 40});
    f.wire({-30, 40}, {-30, 10});
    f.port(model::direction::input, {-30, -10}); // clk
    f.port(model::direction::output, {30, 10});  // q
    t.ports[0].name = "clk";
    t.ports[1].name = "q";
    t.generate_symbol();

    // wrap it once more to check nesting
    auto& w = f.new_cell("wrap");
    f.place_cell(t.id, {0, 0});
    f.port(model::direction::input, pin_of(t, "clk", {0, 0}));
    f.port(model::direction::output, pin_of(t, "q", {0, 0}));
    w.ports[0].name = "clk";
    w.ports[1].name = "q";
    w.generate_symbol();

    auto& top = f.new_cell("top");
    f.place_cell(w.id, {0, 0});
    f.place_cell(w.id, {0, 200});
    f.place("clock", pin_of(w, "clk", {0, 0}) - glm::ivec2{10, 0});
    int q1 = f.port(model::direction::output, pin_of(w, "q", {0, 0}));
    int q2 = f.port(model::direction::output, pin_of(w, "q", {0, 200}));

    sim::simulator s(f.ws, top, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());
    CHECK(s.cells().size() == 3);

    CHECK(s.port_value(q1) == bit(false));
    s.step(); // rising edge for the first one only
    CHECK(s.port_value(q1) == bit(true));
    CHECK(s.port_value(q2) == bit(false));

    auto inner = s.root().child(0)->child(0);
    CHECK(inner && inner->path == "U1/U1" && inner->cell == &t);
    CHECK(s.port_value(*inner, 1) == bit(true));
    auto other = s.root().child(1)->child(0);
    CHECK(other && other->path == "U2/U1");
    CHECK(s.port_value(*other, 1) == bit(false));
    // the clock input of the second one is not connected
    CHECK(s.port_value(*other, 0) == sim::floating(1));

    // a wire inside the toggle cell has a different net per instance
    auto loop = t.get_schematic()->wires[2].id;
    CHECK(s.net_of_wire(*inner, loop) != s.net_of_wire(*other, loop));
    CHECK(s.net_value(s.net_of_wire(*inner, loop)) == bit(false));
    CHECK(s.net_value(s.net_of_wire(*other, loop)) == bit(true));
}

// inv: a W bit not gate between the ports a and y, W a parameter of the
// cell. inv2: an inv of 2 * N bits.
static model::cell& make_inv_cell(fixture& f)
{
    auto& c = f.new_cell("inv");
    c.get_schematic()->params = {model::int_param("W", "W", 1, 1, 64)};
    f.place("not", {0, 0});
    c.get_schematic()->instances[0].param_exprs = {{"width", "W"}};
    f.port(model::direction::input, {-20, 0});
    f.port(model::direction::output, {20, 0});
    c.ports[0].name = "a";
    c.ports[1].name = "y";
    c.ports[0].width_expr = c.ports[1].width_expr = "W";
    CHECK(f.ws.apply_param_defaults(c).empty());
    c.generate_symbol();
    return c;
}

static void test_cell_params()
{
    fixture f;
    auto& inv = make_inv_cell(f);

    auto& inv2 = f.new_cell("inv2");
    inv2.get_schematic()->params = {model::int_param("N", "N", 2, 1, 32)};
    f.place_cell(inv.id, {0, 0});
    inv2.get_schematic()->instances[0].param_exprs = {{"W", "N * 2"}};
    CHECK(f.ws.apply_param_defaults(inv2).empty());
    CHECK((inv2.get_schematic()->instances[0].params == model::param_values{{"W", int64_t(4)}}));
    f.port(model::direction::input, pin_of(inv, "a", {0, 0}), 4);
    f.port(model::direction::output, pin_of(inv, "y", {0, 0}), 4);
    inv2.ports[0].name = "a";
    inv2.ports[1].name = "y";
    inv2.ports[0].width_expr = inv2.ports[1].width_expr = "N * 2";
    inv2.generate_symbol();

    // top: inv with W = 4 and inv2 with N = 8, 16 bits
    auto& top = f.new_cell("top");
    f.place_cell(inv.id, {0, 0}, {{"W", int64_t(4)}});
    f.place_cell(inv2.id, {0, 200}, {{"N", int64_t(8)}});
    int a4 = f.port(model::direction::input, pin_of(inv, "a", {0, 0}), 4);
    int y4 = f.port(model::direction::output, pin_of(inv, "y", {0, 0}), 4);
    int a16 = f.port(model::direction::input, pin_of(inv2, "a", {0, 200}), 16);
    int y16 = f.port(model::direction::output, pin_of(inv2, "y", {0, 200}), 16);

    auto iface = f.ws.interface_of(f.sch().instances[1]);
    CHECK(iface && (*iface.ports)[0].width == 16 && (*iface.ports)[1].width == 16);
    CHECK(netlist::extract(f.ws, top).problems.empty());

    sim::simulator s(f.ws, top, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());
    s.set_input(a4, sim::known(0x5, 4));
    s.set_input(a16, sim::known(0x1234, 16));
    CHECK(s.port_value(y4) == sim::known(0xA, 4));
    CHECK(s.port_value(y16) == sim::known(0xEDCB, 16));

    // each instance has its own copy of the cell for its values, with the
    // same id. the defaults use the cell itself.
    auto u1 = s.root().child(0);
    auto u2 = s.root().child(1);
    auto inner = u2 ? u2->child(0) : nullptr;
    CHECK(u1 && u1->cell != &inv && u1->cell->id == inv.id && u1->cell->ports[0].width == 4);
    CHECK(inner && inner->cell->id == inv.id && inner->cell->ports[0].width == 16);
    CHECK(inner && (inner->cell->get_schematic()->instances[0].params == model::param_values{{"width", int64_t(16)}}));
    CHECK(u2 && u2->cell->get_schematic()->instances[0].params.at("W") == model::param_value(int64_t(16)));
    // the cell itself is unchanged
    CHECK(inv.ports[0].width == 1 && inv.get_schematic()->instances[0].params.empty());

    // a value that makes an expression fail is reported, and wiring the
    // wrong width is a width error
    f.sch().instances[0].params["W"] = int64_t(40);
    inv.get_schematic()->instances[0].param_exprs["width"] = "W * 2 - 81";
    CHECK(f.ws.check_instance(f.sch().instances[0]).empty());
    sim::simulator bad(f.ws, top, builtin::factory());
    bool reported = false;
    for (auto& p : bad.problems())
        reported |= p.find("instance U1: instance U1, width = -1 is out of range") != std::string::npos;
    CHECK(reported);
    inv.ports[0].width_expr = "W / (W - 40)";
    CHECK(f.ws.check_instance(f.sch().instances[0]).find("port a, width: division by zero") != std::string::npos);
    CHECK(!netlist::extract(f.ws, top).problems.empty());
}


static void test_shorted_ports()
{
    // a cell that connects its input straight to its output
    fixture f;
    auto& pass = f.new_cell("pass");
    f.port(model::direction::input, {0, 0});
    f.port(model::direction::output, {40, 0});
    f.wire({0, 0}, {40, 0});
    pass.generate_symbol();

    auto& top = f.new_cell("top");
    f.place_cell(pass.id, {0, 0});
    f.place("not", pin_of(pass, "out1", {0, 0}) + glm::ivec2{20, 0});
    int a = f.port(model::direction::input, pin_of(pass, "in1", {0, 0}));
    int y = f.port(model::direction::output, pin_of(pass, "out1", {0, 0}) + glm::ivec2{40, 0});

    sim::simulator s(f.ws, top, builtin::factory());
    CHECK(s.problems().empty());
    s.set_input(a, bit(true));
    CHECK(s.port_value(y) == bit(false));
    s.set_input(a, bit(false));
    CHECK(s.port_value(y) == bit(true));
}

// pin of a port of a placed instance, through its parameters and transform
static glm::ivec2 instance_pin(fixture& f, int instance, int port)
{
    auto& inst = f.sch().instances[instance];
    auto iface = f.ws.interface_of(inst);
    auto id = (*iface.ports)[port].id;
    return model::pin_position(inst, *iface.symbol->find_pin_of_port(id));
}

// splitter at the origin: bus at (0, 0), end k at (20, -10 (k + 1))
static void test_splitter()
{
    // split
    {
        fixture f;
        f.place("splitter", {0, 0}, {{"width", int64_t(8)}});
        int bus = f.port(model::direction::input, {0, 0}, 8);
        int lo = f.port(model::direction::output, {20, -10}, 4);
        int hi = f.port(model::direction::output, {20, -20}, 4);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(bus, sim::known(0xA5, 8));
        CHECK(s.port_value(lo) == sim::known(0x5, 4));
        CHECK(s.port_value(hi) == sim::known(0xA, 4));
    }

    // merge, an undriven end leaves its bits z
    {
        fixture f;
        f.place("splitter", {0, 0}, {{"width", int64_t(8)}});
        int lo = f.port(model::direction::input, {20, -10}, 4);
        // not driven until set
        int hi = f.port(model::direction::inout, {20, -20}, 4);
        int bus = f.port(model::direction::output, {0, 0}, 8);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(lo, sim::known(0x3, 4));
        CHECK(s.port_value(bus) == value(8, 0x03, 0, 0xF0));
        s.set_input(hi, sim::known(0xC, 4));
        CHECK(s.port_value(bus) == sim::known(0xC3, 8));
    }

    // two splitters back to back swap the nibbles, a gate behind them sees
    // the result in the same step as if it was a wire
    {
        fixture f;
        f.place("splitter", {0, 0}, {{"width", int64_t(8)}});
        model::param_values swap{{"width", int64_t(8)}, {"bits", std::string("7:4, 3:0")}};
        f.place("splitter", {40, 0}, swap);
        f.sch().instances[1].xf.mirror = true;
        CHECK(instance_pin(f, 1, 1) == glm::ivec2(20, -10));
        CHECK(instance_pin(f, 1, 0) == glm::ivec2(40, 0));

        f.place("not", {80, 0}, {{"width", int64_t(8)}});
        f.wire({40, 0}, instance_pin(f, 2, 0));
        int in = f.port(model::direction::input, {0, 0}, 8);
        int swapped = f.port(model::direction::output, {40, 0}, 8);
        int out = f.port(model::direction::output, instance_pin(f, 2, 1), 8);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        for (auto& p : s.problems())
            std::printf("  %s\n", p.c_str());
        CHECK(s.problems().empty());
        s.set_input(in, sim::known(0xA5, 8));
        CHECK(s.port_value(swapped) == sim::known(0x5A, 8));
        CHECK(s.port_value(out) == sim::known(0xA5, 8));

        // one bit of the bus driven twice: from the input and from the output
        // side through the second splitter
        f.sch().instances.pop_back();
        f.top->ports.pop_back();
        f.top->ports[swapped].dir = model::direction::inout;
        sim::simulator s2(f.ws, *f.top, builtin::factory());
        s2.set_input(in, sim::known(0xA5, 8));
        s2.set_input(swapped, value(8, 0x01, 0, 0xFE)); // bit 0 of the swapped bus is bit 4 of the input, a 0
        CHECK(s2.port_value(in) == value(8, 0xA5 & ~0x10, 0x10, 0));
        CHECK(s2.port_value(swapped) == value(8, 0x5A & ~0x01, 0x01, 0));
    }

    // ends of the same bit are joined even without a bus
    {
        fixture f;
        f.place("splitter", {0, 0}, {{"width", int64_t(4)}, {"bits", std::string("0, 0")}});
        int a = f.port(model::direction::input, {20, -10});
        int b = f.port(model::direction::output, {20, -20});

        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(a, bit(true));
        CHECK(s.port_value(b) == bit(true));
    }

    // inside a cell used twice in a row: swapping twice gives the input back
    {
        fixture f;
        auto& sw = f.new_cell("swap");
        f.place("splitter", {0, 0}, {{"width", int64_t(8)}});
        model::param_values swap{{"width", int64_t(8)}, {"bits", std::string("7:4, 3:0")}};
        f.place("splitter", {40, 0}, swap);
        f.sch().instances[1].xf.mirror = true;
        f.port(model::direction::input, {0, 0}, 8);
        f.port(model::direction::output, {40, 0}, 8);
        sw.generate_symbol();

        auto& top = f.new_cell("top");
        f.place_cell(sw.id, {0, 0});
        f.place_cell(sw.id, pin_of(sw, "out1", {0, 0}) - pin_of(sw, "in1", {0, 0}));
        int in = f.port(model::direction::input, pin_of(sw, "in1", {0, 0}), 8);
        int out = f.port(model::direction::output, pin_of(sw, "out1", f.sch().instances[1].pos), 8);

        sim::simulator s(f.ws, top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(in, sim::known(0x12, 8));
        CHECK(s.port_value(*s.root().child(0), 1) == sim::known(0x21, 8));
        CHECK(s.port_value(out) == sim::known(0x12, 8));
    }
}

static void test_splitter_params()
{
    fixture f;
    auto ports = [&](model::param_values v) {
        model::instance i;
        i.cell = model::builtin_cell_id("splitter");
        i.params = std::move(v);
        std::vector<int> widths;
        for (auto& p : *f.ws.interface_of(i).ports)
            widths.push_back(p.width);
        return widths;
    };

    // even split, the first ends get the extra bits
    CHECK(ports({{"width", int64_t(8)}, {"fanout", int64_t(3)}}) == (std::vector<int>{8, 3, 3, 2}));
    // fan out above the width: one end per bit
    CHECK(ports({{"width", int64_t(2)}, {"fanout", int64_t(5)}}) == (std::vector<int>{2, 1, 1}));
    CHECK(ports({{"width", int64_t(8)}, {"bits", std::string("0, 1,7-2")}}) == (std::vector<int>{8, 1, 1, 6}));
    // invalid bits fall back to the even split
    CHECK(ports({{"width", int64_t(8)}, {"bits", std::string("8:0")}}) == (std::vector<int>{8, 4, 4}));
    CHECK(ports({{"width", int64_t(8)}, {"bits", std::string("1,,2")}}) == (std::vector<int>{8, 4, 4}));

    auto prim = f.ws.primitive_of(*f.ws.find_cell(model::builtin_cell_id("splitter")));
    auto check = [&](const char* bits) {
        return prim->check(model::resolve_params(prim->params(), {{"width", int64_t(8)}, {"bits", std::string(bits)}}));
    };
    CHECK(check("").empty());
    CHECK(check("7:0").empty());
    CHECK(!check("8").empty());
    CHECK(!check("1;2").empty());
}


// primitives only used by the tests, next to the builtin ones
namespace test_prims {

using model::direction;

// io (inout), oe, d, q. drives d onto io while oe is 1, q is what io reads
class bidir_component : public sim::component {
public:
    void eval(std::span<value> ports, sim::context&) override
    {
        ports[3] = ports[0];
        ports[0] = ports[1].get(0) == sim::bit_state::one ? ports[2] : sim::floating(8);
    }
};

class bidir : public builtin::primitive {
public:
    std::string kind() const override { return "test_bidir"; }
    std::string name() const override { return "bidir"; }

    model::cell_interface build(const model::param_values&) const override
    {
        builtin::builder b;
        b.rect({-10, -20}, {10, 20});
        b.port("io", direction::inout, 8, {20, 0});
        b.port("oe", direction::input, 1, {-20, 10});
        b.port("d", direction::input, 8, {-20, 0});
        b.port("q", direction::output, 8, {20, 10});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return std::make_unique<bidir_component>(); }
};

// asks to be woken at the current time forever
class spinner_component : public sim::component {
public:
    void eval(std::span<value> ports, sim::context& ctx) override
    {
        ctx.wake_at(ctx.now());
        ports[0] = sim::known(0, 1);
    }
};

class spinner : public builtin::primitive {
public:
    std::string kind() const override { return "test_spinner"; }
    std::string name() const override { return "spinner"; }

    model::cell_interface build(const model::param_values&) const override
    {
        builtin::builder b;
        b.port("out", direction::output, 1, {10, 0});
        return b.finish();
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return std::make_unique<spinner_component>(); }
};

const model::primitive_registry& registry()
{
    static const model::primitive_registry r = [] {
        model::primitive_registry r;
        builtin::add_sources(r);
        builtin::add_gates(r);
        builtin::add_memory(r);
        builtin::add_wiring(r);
        r.add(std::make_unique<bidir>());
        r.add(std::make_unique<spinner>());
        return r;
    }();
    return r;
}

class factory_t : public sim::component_factory {
public:
    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override
    {
        auto b = std::get_if<model::builtin_impl>(&info.cell.impl);
        auto p = b ? static_cast<const builtin::primitive*>(registry().find(b->kind)) : nullptr;
        return p ? p->create(info, error) : nullptr;
    }
};

const sim::component_factory& factory()
{
    static const factory_t f;
    return f;
}

}

static value hex(const char* s, int width)
{
    auto v = sim::parse_hex(s, width);
    if (!v) {
        std::printf("bad hex %s\n", s);
        failures++;
        return sim::unknown(width);
    }
    return *v;
}

static void test_wide_values()
{
    auto a = hex("123456789ABCDEF0_FEDCBA9876543210", 128);
    CHECK(a.width() == 128 && a.words() == 2);
    CHECK(a.bits(0) == 0xFEDCBA9876543210ull);
    CHECK(a.bits(1) == 0x123456789ABCDEF0ull);
    CHECK(sim::to_string(a) == "0x123456789ABCDEF0FEDCBA9876543210");

    // across the word boundary
    CHECK(a.slice(60, 8) == sim::known(0x0F, 8));
    CHECK(a.slice(64, 64) == sim::known(0x123456789ABCDEF0ull, 64));
    auto b = sim::known(0, 128);
    b.insert(60, sim::known(0xAB, 8));
    CHECK(b.bits(0) == 0xB000000000000000ull && b.bits(1) == 0xA);
    // beyond the width it's z, and inserting cuts at the width
    CHECK(sim::known(1, 4).slice(2, 4) == value(4, 0, 0, 0b1100));
    auto c = sim::known(0, 70);
    c.insert(66, sim::known(0xFF, 8));
    CHECK(c.bits(1) == 0b111100 && c.bits(0) == 0);

    // single bits and states
    auto d = sim::known(0, 130);
    d.set(129, sim::bit_state::one);
    d.set(64, sim::bit_state::x);
    d.set(3, sim::bit_state::z);
    CHECK(d.get(129) == sim::bit_state::one);
    CHECK(d.get(64) == sim::bit_state::x);
    CHECK(d.get(3) == sim::bit_state::z);
    CHECK(d.get(5) == sim::bit_state::zero);
    CHECK(!d.fully_known());
    // a digit with some z bits is x, z only when all four are
    CHECK(sim::to_hex(d) == "2" + std::string(15, '0') + "x" + std::string(15, '0') + "x");
    CHECK(sim::to_hex(sim::floating(8)) == "zz");

    CHECK(sim::floating(130).fully_floating());
    CHECK(!sim::unknown(130).fully_known());
    CHECK(sim::known(0, 100) != sim::known(0, 101));
    CHECK(value().fully_floating() && value().width() == 0);

    // two drivers each driving one half
    auto lo = sim::floating(128);
    lo.insert(0, sim::known(0x1111, 64));
    auto hi = sim::floating(128);
    hi.insert(64, sim::known(0x2222, 64));
    auto r = sim::resolve(std::vector<value>{lo, hi}, 128);
    CHECK(r.bits(0) == 0x1111 && r.bits(1) == 0x2222 && r.fully_known());
    hi.insert(0, sim::known(0, 1));
    CHECK(sim::resolve(std::vector<value>{lo, hi}, 128).get(0) == sim::bit_state::x);

    CHECK(sim::parse_hex("0xFF", 8) == sim::known(0xFF, 8));
    CHECK(!sim::parse_hex("1FF", 8));
    CHECK(sim::parse_hex("01FF", 9) == sim::known(0x1FF, 9));
    CHECK(!sim::parse_hex("zz", 8));
    CHECK(!sim::parse_hex("", 8));
}

static void test_wide_circuits()
{
    // 128 bit and gate
    {
        fixture f;
        f.place("and", {0, 0}, {{"width", int64_t(128)}});
        int a = f.port(model::direction::input, {-30, 10}, 128);
        int b = f.port(model::direction::input, {-30, -10}, 128);
        int y = f.port(model::direction::output, {30, 0}, 128);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(a, hex("FFFF0000FFFF0000_00000000FFFFFFFF", 128));
        s.set_input(b, hex("0F0F0F0F0F0F0F0F_123456789ABCDEF0", 128));
        CHECK(s.port_value(y) == hex("0F0F00000F0F0000_000000009ABCDEF0", 128));

        // x in the upper word only makes upper bits unknown
        auto ax = s.input_value(a);
        ax.set(64, sim::bit_state::x);
        s.set_input(a, ax);
        auto v = s.port_value(y);
        CHECK(v.get(64) == sim::bit_state::x);
        CHECK(v.bits(0) == 0x9ABCDEF0);
    }

    // splitter from 128 bits into two halves
    {
        fixture f;
        f.place("splitter", {0, 0}, {{"width", int64_t(128)}});
        int bus = f.port(model::direction::input, {0, 0}, 128);
        int lo = f.port(model::direction::output, {20, -10}, 64);
        int hi = f.port(model::direction::output, {20, -20}, 64);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(bus, hex("AAAAAAAAAAAAAAAA5555555555555555", 128));
        CHECK(s.port_value(lo) == sim::known(0x5555555555555555ull, 64));
        CHECK(s.port_value(hi) == sim::known(0xAAAAAAAAAAAAAAAAull, 64));
    }

    // a 100 bit register and a not gate
    {
        fixture f;
        f.place("register", {100, 0}, {{"width", int64_t(100)}});
        f.place("clock", {60, -10});
        f.place("not", {150, 10}, {{"width", int64_t(100)}});
        int d = f.port(model::direction::input, {70, 10}, 100);
        int q = f.port(model::direction::output, {170, 10}, 100);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().empty());
        CHECK(s.port_value(q) == hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFF", 100));
        s.set_input(d, hex("1_00000000_00000000_00000001", 100));
        s.step();
        CHECK(s.port_value(q) == hex("E_FFFFFFFF_FFFFFFFF_FFFFFFFE", 100));
    }
}

static void test_time()
{
    CHECK(sim::format_time(0) == "0 s");
    CHECK(sim::format_time(12) == "12 ps");
    CHECK(sim::format_time(1500 * sim::us) == "1.5 ms");
    CHECK(sim::format_time(2 * sim::seconds) == "2 s");
    CHECK(sim::format_time(sim::seconds + 2 * sim::us) == "1.000002 s");

    // a 1 kHz clock and a 250 Hz clock with 25% duty
    fixture f;
    f.place("clock", {0, 0}, {{"frequency", int64_t(1000)}});
    f.place("clock", {0, 100}, {{"frequency", int64_t(250)}, {"duty", int64_t(25)}});
    int a = f.port(model::direction::output, {10, 0});
    int b = f.port(model::direction::output, {10, 100});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());

    CHECK(s.now() == 0);
    CHECK(s.port_value(a) == bit(false) && s.port_value(b) == bit(false));
    CHECK(s.next_event() == 500 * sim::us);

    CHECK(s.step());
    CHECK(s.now() == 500 * sim::us);
    CHECK(s.port_value(a) == bit(true));
    s.step();
    CHECK(s.now() == sim::ms);
    CHECK(s.port_value(a) == bit(false));

    // a falls and b rises at 3 ms
    CHECK(s.run_until(3 * sim::ms));
    CHECK(s.now() == 3 * sim::ms);
    CHECK(s.port_value(a) == bit(false));
    CHECK(s.port_value(b) == bit(true));
    // time passes even without events
    CHECK(s.run_until(3 * sim::ms + 100 * sim::us));
    CHECK(s.now() == 3 * sim::ms + 100 * sim::us);
    s.run_until(4 * sim::ms);
    CHECK(s.port_value(b) == bit(false));

    s.reset();
    CHECK(s.now() == 0);
    CHECK(s.port_value(a) == bit(false));
    CHECK(s.next_event() == 500 * sim::us);

    // stops after 3 points in time
    CHECK(!s.run_until(sim::seconds, 3));
    CHECK(s.now() == 1500 * sim::us);

    // a component waking itself at the same time forever
    fixture g(test_prims::registry());
    g.place("test_spinner", {0, 0});
    sim::simulator t(g.ws, *g.top, test_prims::factory());
    CHECK(t.problems().empty());
    CHECK(!t.run_until(sim::seconds));
    CHECK(t.oscillating());
    CHECK(t.now() == 0);
}

// two tri-state buffers on one net with a pull resistor
static void test_tristate_bus()
{
    for (bool up : {true, false}) {
        fixture f;
        // tristate at (0, 0): in (-20, 0), en (0, -20), out (20, 0)
        f.place("tristate", {0, 0});
        f.place("tristate", {0, 100});
        f.wire({20, 0}, {40, 0});
        f.wire({40, 0}, {40, 50});
        f.wire({40, 50}, {40, 100});
        f.wire({40, 100}, {20, 100});
        f.place("pull", {40, 50}, {{"direction", std::string(up ? "up" : "down")}});
        int a = f.port(model::direction::input, {-20, 0});
        int ea = f.port(model::direction::input, {0, -20});
        int b = f.port(model::direction::input, {-20, 100});
        int eb = f.port(model::direction::input, {0, 80});
        int y = f.port(model::direction::output, {20, 0});
        sim::simulator s(f.ws, *f.top, builtin::factory());
        for (auto& p : s.problems())
            std::printf("  %s\n", p.c_str());
        CHECK(s.problems().empty());

        // nobody drives: the pull
        CHECK(s.port_value(y) == bit(up));

        s.set_input(ea, bit(true));
        s.set_input(a, bit(!up));
        CHECK(s.port_value(y) == bit(!up));

        // both drive different values
        s.set_input(eb, bit(true));
        s.set_input(b, bit(up));
        CHECK(s.port_value(y) == sim::unknown(1));

        s.set_input(ea, bit(false));
        CHECK(s.port_value(y) == bit(up));

        // unknown enable
        s.set_input(eb, sim::unknown(1));
        CHECK(s.port_value(y) == sim::unknown(1));
        CHECK(!s.oscillating());
    }

    // pull up and down on the same net: floating bits are unknown
    {
        fixture f;
        f.place("pull", {0, 0}, {{"direction", std::string("up")}});
        f.place("pull", {40, 0});
        f.wire({0, 0}, {40, 0});
        int y = f.port(model::direction::output, {40, 0});
        int in = f.port(model::direction::inout, {0, 0});
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.port_value(y) == sim::unknown(1));
        s.set_input(in, bit(true));
        CHECK(s.port_value(y) == bit(true));
    }
}

static void test_inout()
{
    fixture f(test_prims::registry());
    // bidir at (0, 0): io (20, 0), oe (-20, 10), d (-20, 0), q (20, 10)
    f.place("test_bidir", {0, 0});
    int bus = f.port(model::direction::inout, {20, 0}, 8);
    int oe = f.port(model::direction::input, {-20, 10});
    int d = f.port(model::direction::input, {-20, 0}, 8);
    int q = f.port(model::direction::output, {20, 10}, 8);

    // a second one without a net on io reads what it drives itself
    f.place("test_bidir", {0, 100});
    f.place("constant", {-30, 110});
    f.place("constant", {-30, 100}, {{"width", int64_t(8)}, {"value", int64_t(0x33)}});
    int q2 = f.port(model::direction::output, {20, 110}, 8);

    sim::simulator s(f.ws, *f.top, test_prims::factory());
    CHECK(s.problems().empty());
    CHECK(s.port_value(q2) == sim::known(0x33, 8));

    // nobody drives the bus
    CHECK(s.port_value(q) == sim::floating(8));

    // the top cell drives it
    s.set_input(bus, sim::known(0x5A, 8));
    CHECK(s.port_value(q) == sim::known(0x5A, 8));

    // the component drives it and reads its own value back
    s.set_input(bus, sim::floating(8));
    s.set_input(d, sim::known(0x0F, 8));
    s.set_input(oe, bit(true));
    CHECK(s.port_value(bus) == sim::known(0x0F, 8));
    CHECK(s.port_value(q) == sim::known(0x0F, 8));

    // both drive: the bits that differ are unknown
    s.set_input(bus, sim::known(0xFF, 8));
    CHECK(s.port_value(q) == value(8, 0x0F, 0xF0, 0));

    // and it lets go again
    s.set_input(oe, bit(false));
    CHECK(s.port_value(q) == sim::known(0xFF, 8));
    CHECK(!s.oscillating());
}

static void test_inspect_state()
{
    fixture f;
    f.place("register", {100, 0});
    f.place("clock", {60, -10});
    int d = f.port(model::direction::input, {70, 10}, 8);
    sim::simulator s(f.ws, *f.top, builtin::factory());
    s.set_input(d, sim::known(0x42, 8));
    s.step();
    auto items = s.inspect(s.root(), 0);
    CHECK(items.size() == 1 && items[0].name == "q" && items[0].v == sim::known(0x42, 8));
    CHECK(s.inspect(s.root(), 5).empty());

    // the register's state survives a copy to a new component
    auto params = model::resolve_params(builtin::registry().find("register")->params(), {{"width", int64_t(70)}});
    std::string error;
    auto reg_cell = f.ws.find_cell(model::builtin_cell_id("register"));
    auto r1 = builtin::factory().create({*reg_cell, params, {}}, error);
    std::vector<value> ports{hex("2_00000000_00000001", 70), sim::known(0, 1), sim::floating(70)};
    sim::context ctx(0, false, sim::never);
    r1->eval(ports, ctx);
    ports[1] = sim::known(1, 1);
    r1->eval(ports, ctx);
    CHECK(ports[2] == hex("2_00000000_00000001", 70));

    std::vector<uint8_t> bytes;
    CHECK(r1->save_state(bytes));
    auto r2 = builtin::factory().create({*reg_cell, params, {}}, error);
    CHECK(r2->load_state(bytes));
    CHECK(!r2->load_state(std::span<const uint8_t>(bytes.data(), 3)));
    // clk stays 1: no edge, q comes from the loaded state
    ports[0] = sim::known(0, 70);
    r2->eval(ports, ctx);
    CHECK(ports[2] == hex("2_00000000_00000001", 70));
}

// waits until the runner published a state that passes the check
template <class F>
static bool eventually(sim::runner& r, F check)
{
    for (int i = 0; i < 400; i++) {
        r.sync();
        if (check()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static void test_runner()
{
    using namespace std::chrono_literals;

    // toggle flip flop on a 1 kHz clock: q flips at every rising edge
    fixture f;
    f.place("register", {0, 0}, {{"width", int64_t(1)}});
    f.place("clock", {-40, -10}, {{"frequency", int64_t(1000)}});
    f.place("not", {60, 10});
    f.wire({30, 10}, {40, 10});
    f.wire({80, 10}, {90, 10});
    f.wire({90, 10}, {90, 40});
    f.wire({90, 40}, {-30, 40});
    f.wire({-30, 40}, {-30, 10});
    int q = f.port(model::direction::output, {30, 10});

    auto make = [&] { return std::make_unique<sim::simulator>(f.ws, *f.top, builtin::factory()); };
    // q after the rising edges up to t
    auto expect_q = [](sim::time t) {
        auto rising = (t / (500 * sim::us) + 1) / 2;
        return bit(rising % 2 == 1);
    };

    {
        sim::runner r(make());
        CHECK(r.view().now() == 0);
        r.step();
        r.wait_idle();
        CHECK(r.view().now() == 500 * sim::us);
        CHECK(r.view().port_value(q) == bit(true));

        // as fast as possible, then pause
        r.set_speed(0.0);
        r.run(true);
        CHECK(r.running());
        CHECK(eventually(r, [&] { return r.view().now() > 20 * sim::ms; }));
        r.run(false);
        r.post([](sim::simulator&) {});
        r.wait_idle();
        auto t = r.view().now();
        CHECK(r.view().port_value(q) == expect_q(t));
        // paused: time stands still
        std::this_thread::sleep_for(20ms);
        r.sync();
        CHECK(r.view().now() == t);

        r.reset();
        r.wait_idle();
        CHECK(r.view().now() == 0);
        CHECK(r.view().port_value(q) == bit(false));

        // real time, loosely: 100 ms of real time are about 100 ms simulated
        r.set_speed(1.0);
        r.run(true);
        std::this_thread::sleep_for(100ms);
        r.run(false);
        r.post([](sim::simulator&) {});
        r.wait_idle();
        t = r.view().now();
        CHECK(t > 30 * sim::ms && t < 500 * sim::ms);
        CHECK(r.view().port_value(q) == expect_q(t));
    }

    // commands are applied in order
    {
        fixture g;
        g.place("and", {0, 0});
        int a = g.port(model::direction::input, {-30, 10});
        int b = g.port(model::direction::input, {-30, -10});
        int y = g.port(model::direction::output, {30, 0});
        sim::runner r(std::make_unique<sim::simulator>(g.ws, *g.top, builtin::factory()));
        for (int i = 0; i < 1000; i++) {
            r.set_input(a, bit(i & 1));
            r.set_input(b, bit(i & 2));
        }
        r.set_input(a, bit(true));
        r.set_input(b, bit(true));
        r.wait_idle();
        CHECK(r.view().port_value(y) == bit(true));
        CHECK(r.view().input_value(a) == bit(true));
    }

    // oscillation pauses
    {
        fixture g(test_prims::registry());
        g.place("test_spinner", {0, 0});
        sim::runner r(std::make_unique<sim::simulator>(g.ws, *g.top, test_prims::factory()));
        r.run(true);
        CHECK(eventually(r, [&] { return !r.running(); }));
        CHECK(r.view().oscillating());
    }

    // started and destroyed right away, while running
    for (int i = 0; i < 50; i++) {
        sim::runner r(make());
        r.set_speed(i % 2 ? 0.0 : 1.0);
        r.run(true);
        if (i % 3 == 0) r.step();
    }
}

static std::vector<uint8_t> bytes_of(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

// a minimal little endian elf32: one loadable segment and a symbol table
static std::vector<uint8_t> tiny_elf()
{
    std::vector<uint8_t> e(512, 0);
    auto u16 = [&](size_t at, uint16_t v) { e[at] = uint8_t(v), e[at + 1] = uint8_t(v >> 8); };
    auto u32 = [&](size_t at, uint32_t v) {
        for (int k = 0; k < 4; k++)
            e[at + k] = uint8_t(v >> (8 * k));
    };
    std::memcpy(e.data(), "\x7f" "ELF", 4);
    e[4] = 1; // 32 bit
    e[5] = 1; // little endian
    e[6] = 1;
    u16(16, 2);         // executable
    u32(24, 0x1000);    // entry
    u32(28, 52);        // program headers
    u32(32, 200);       // section headers
    u16(40, 52);        // header size
    u16(42, 32);        // program header size
    u16(44, 1);         // one
    u16(46, 40);        // section header size
    u16(48, 3);         // null, symtab, strtab
    // the segment: 4 bytes at file offset 100 to address 0x1000
    u32(52, 1);
    u32(56, 100);
    u32(64, 0x1000);
    u32(68, 4);
    u32(72, 4);
    std::memcpy(e.data() + 100, "\x01\x02\x03\x04", 4);
    // symbols at 120: null, main = 0x1000 (func)
    u32(136, 1);
    u32(140, 0x1000);
    e[148] = 0x12; // global func
    // strings at 160
    std::memcpy(e.data() + 160, "\0main\0", 6);
    // section 1: symtab, link 2
    u32(240 + 4, 2);
    u32(240 + 16, 120);
    u32(240 + 20, 32);
    u32(240 + 24, 2);
    u32(240 + 36, 16);
    // section 2: strtab
    u32(280 + 4, 3);
    u32(280 + 16, 160);
    u32(280 + 20, 6);
    return e;
}

static void test_firmware()
{
    io::firmware f;
    std::string error;

    // two data records, an extended linear address, a start address
    auto hex = ":0400000001020304F2\n"
               ":020000040001F9\n"
               ":02001000AABB89\n"
               ":0400000500000100F6\n"
               ":00000001FF\n";
    CHECK(io::parse_firmware(bytes_of(hex), io::firmware_format::automatic, f, error));
    CHECK(f.segments.size() == 2);
    if (f.segments.size() == 2) {
        CHECK(f.segments[0].address == 0 && f.segments[0].bytes == (std::vector<uint8_t>{1, 2, 3, 4}));
        CHECK(f.segments[1].address == 0x10010 && f.segments[1].bytes == (std::vector<uint8_t>{0xAA, 0xBB}));
    }
    CHECK(f.has_entry && f.entry == 0x100);
    CHECK(f.end() == 0x10012);
    CHECK(f.flatten(2, 4, 0) == (std::vector<uint8_t>{3, 4, 0, 0}));

    CHECK(!io::parse_firmware(bytes_of(":0400000001020304F3\n"), io::firmware_format::intel_hex, f, error));
    CHECK(error.find("checksum") != std::string::npos);

    // s-records: S1 data, S9 start
    auto srec = "S1070000AABBCCDDEA\nS9030100FB\n";
    CHECK(io::parse_firmware(bytes_of(srec), io::firmware_format::automatic, f, error));
    CHECK(f.segments.size() == 1 && f.segments[0].bytes == (std::vector<uint8_t>{0xAA, 0xBB, 0xCC, 0xDD}));
    CHECK(f.has_entry && f.entry == 0x100);

    // binary at a base address
    CHECK(io::parse_firmware({9, 8, 7}, io::firmware_format::binary, f, error, 0x8000));
    CHECK(f.segments.size() == 1 && f.segments[0].address == 0x8000 && f.segments[0].bytes.size() == 3);

    error.clear();
    CHECK(io::parse_firmware(tiny_elf(), io::firmware_format::automatic, f, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(f.segments.size() == 1 && f.segments[0].address == 0x1000 && f.segments[0].bytes == (std::vector<uint8_t>{1, 2, 3, 4}));
    CHECK(f.has_entry && f.entry == 0x1000);
    CHECK(f.symbols.size() == 1 && f.symbols[0x1000] == "main");
}

static void test_io_parts()
{
    fixture f;
    // button at (0, 0): out (20, 0), switch at (0, 100)
    f.place("button", {0, 0});
    f.place("switch", {0, 100}, {{"active_low", true}});
    f.place("led", {60, 0});
    int b = f.port(model::direction::output, {20, 0});
    int sw = f.port(model::direction::output, {20, 100});
    f.wire({20, 0}, {40, 0});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());

    CHECK(s.port_value(b) == bit(false));
    CHECK(s.port_value(sw) == bit(true));
    CHECK(s.interact(s.root(), 0, "press", 0));
    CHECK(s.port_value(b) == bit(true));
    s.interact(s.root(), 0, "release", 0);
    CHECK(s.port_value(b) == bit(false));
    s.interact(s.root(), 1, "press", 0);
    CHECK(s.port_value(sw) == bit(false));
    s.interact(s.root(), 1, "press", 0);
    CHECK(s.port_value(sw) == bit(true));
    // the led has nothing to interact with
    CHECK(!s.interact(s.root(), 5, "press", 0));

    // two terminals: what one sends the other shows
    fixture g;
    // terminal at (0, 0): rx (-30, 10), tx (-30, -10). the second at
    // (0, 100), its rx gets the first's tx.
    g.place("terminal", {0, 0}, {{"baud", int64_t(1000)}});
    g.place("terminal", {0, 100}, {{"baud", int64_t(1000)}});
    g.wire({-30, -10}, {-60, -10});
    g.wire({-60, -10}, {-60, 110});
    g.wire({-60, 110}, {-30, 110});
    sim::simulator t(g.ws, *g.top, builtin::factory());
    CHECK(t.problems().empty());
    t.interact(t.root(), 0, "key", 'H');
    t.interact(t.root(), 0, "key", 'i');
    t.run_until(30 * sim::ms);
    auto items = t.inspect(t.root(), 1);
    CHECK(items.size() == 1 && items[0].name == "screen" && items[0].text == "Hi");
    CHECK(t.next_event() == sim::never);
}

// a multiplexed 7-segment display: the share of time each segment is lit
static void test_seg7_view()
{
    fixture f;
    // hex input on the left at (-30, 0), en at (0, -50)
    f.place("seg7_display", {0, 0}, {{"input", std::string("hex")}, {"enable", true}});
    // constant 1 (segments b and c), out at (-30, 0)
    f.place("constant", {-40, 0}, {{"width", int64_t(4)}, {"value", int64_t(1)}});
    // 1 a quarter of the time, out at (0, -50)
    f.place("clock", {-10, -50}, {{"frequency", int64_t(1000)}, {"duty", int64_t(25)}});
    sim::simulator s(f.ws, *f.top, builtin::factory());
    CHECK(s.problems().empty());
    int k = s.root().components[0];

    s.run_until(10 * sim::ms);
    auto views = s.sample_views();
    CHECK(views.size() == 1 && views.count(k) && views[k].size() == 8);
    auto& v = views[k];
    CHECK(std::abs(v[1] - 0.25f) < 0.02f && std::abs(v[2] - 0.25f) < 0.02f);
    CHECK(v[0] == 0.0f && v[3] == 0.0f && v[6] == 0.0f && v[7] == 0.0f);

    // no time passed: what is lit now (the clock is low for 0.75 ms, then
    // high)
    s.run_until(10 * sim::ms + sim::ms * 8 / 10);
    s.sample_views();
    auto now = s.sample_views()[k];
    CHECK(now[1] == 1.0f && now[2] == 1.0f && now[0] == 0.0f);

    // a new window after a reset
    s.reset();
    s.run_until(sim::ms);
    auto again = s.sample_views()[k];
    CHECK(std::abs(again[1] - 0.25f) < 0.02f);
}

static void test_rom_ram()
{
    auto dir = std::filesystem::temp_directory_path() / "logic_sim_test";
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / "rom.hex");
        out << ":040000001122334452\n:00000001FF\n";
    }

    fixture f;
    f.top = &f.ws.create_cell(*f.ws.find_library("work"), "mem");
    f.ws.find_library("work")->dir = dir;
    // rom at (0, 0): addr (-40, 10), data (40, 10), cs (-40, -10)
    f.place("rom", {0, 0}, {{"file", std::string("rom.hex")}, {"addr_width", int64_t(4)}});
    int a = f.port(model::direction::input, {-40, 10}, 4);
    int d = f.port(model::direction::output, {40, 10}, 8);
    int cs = f.port(model::direction::input, {-40, -10});

    // ram at (0, 200): addr (-40, 210), data (40, 210), cs (-40, 190),
    // we (-40, 180)
    f.place("ram", {0, 200}, {{"addr_width", int64_t(4)}});
    int ra = f.port(model::direction::input, {-40, 210}, 4);
    int rd = f.port(model::direction::inout, {40, 210}, 8);
    int we = f.port(model::direction::input, {-40, 180});

    sim::simulator s(f.ws, *f.top, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());

    s.set_input(cs, bit(true));
    s.set_input(a, sim::known(2, 4));
    CHECK(s.port_value(d) == sim::known(0x33, 8));
    s.set_input(cs, bit(false));
    CHECK(s.port_value(d) == sim::floating(8));

    // the debugger sees the contents
    auto comp = s.component_of(s.root(), 0);
    auto dbg = comp ? comp->debug() : nullptr;
    CHECK(dbg && dbg->memories().size() == 1 && dbg->memories()[0].size == 16);
    uint8_t buf[4];
    CHECK(dbg && dbg->read_memory(0, 0, buf) && buf[0] == 0x11 && buf[3] == 0x44);

    // write 0x5A to 7, then read it back
    s.set_input(ra, sim::known(7, 4));
    s.set_input(rd, sim::known(0x5A, 8));
    s.set_input(we, bit(true));
    s.set_input(we, bit(false));
    s.set_input(rd, sim::floating(8));
    CHECK(s.port_value(rd) == sim::known(0x5A, 8));

    // a missing file is a problem
    f.sch().instances[0].params["file"] = std::string("nothing.hex");
    sim::simulator t(f.ws, *f.top, builtin::factory());
    CHECK(t.problems().size() == 1 && t.problems()[0].find("nothing.hex") != std::string::npos);
    std::filesystem::remove_all(dir);
}

static void test_waveforms()
{
    fixture f;
    f.place("clock", {0, 0}, {{"frequency", int64_t(1000)}});
    int out = f.port(model::direction::output, {10, 0});
    int bus = f.port(model::direction::inout, {100, 100}, 70);
    f.port(model::direction::output, {100, 100}, 70);
    auto make = [&] { return std::make_unique<sim::simulator>(f.ws, *f.top, builtin::factory()); };
    auto s = make();
    int net = s->net_of_port(s->root(), out);
    int bus_net = s->net_of_port(s->root(), bus);
    s->waveforms.add(*s, net, "clk");
    s->waveforms.add(*s, net, "alias");
    s->waveforms.add(*s, bus_net, "bus");
    s->waveforms.add(*s, -1, "unconnected");
    CHECK(s->waveforms.size() == 2 && s->waveforms.contains(net) && !s->waveforms.contains(-1));
    if (s->waveforms.size() != 2) return;

    // a mirror kept up to date incrementally, like the waveform window's
    wave::trace_set mirror;
    sim::waveform_recorder::sync_state state;
    auto same_as_snapshot = [&] {
        s->waveforms.sync(mirror, state);
        auto data = s->waveforms.snapshot();
        if (mirror.traces.size() != data.traces.size() || mirror.end != data.end) return false;
        for (size_t k = 0; k < data.traces.size(); k++) {
            auto &a = mirror.traces[k], &b = data.traces[k];
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++)
                if (a.at(i) != b.at(i) || a.value(i) != b.value(i)) return false;
        }
        return true;
    };
    CHECK(same_as_snapshot());

    // Forty clock edges in one advance: display snapshots would miss these.
    s->run_until(20 * sim::ms);
    auto data = s->waveforms.snapshot();
    CHECK(data.end == 20 * sim::ms);
    CHECK(data.traces[0].size() == 41);
    for (int k = 0; k <= 40; k++) {
        CHECK(data.traces[0].at(k) == k * 500 * sim::us);
        CHECK(data.traces[0].value(k) == bit(k % 2));
    }
    CHECK(same_as_snapshot());
    CHECK(data.traces[0].index_at(750 * sim::us) == 1);
    CHECK(data.traces[0].first_after(750 * sim::us) == 2);

    // Wide buses, x/z, and multiple actions at the same simulated timestamp.
    CHECK(data.traces[1].value(0) == sim::floating(70));
    s->set_input(bus, sim::known(0xAB, 70));
    CHECK(same_as_snapshot());
    s->set_input(bus, sim::unknown(70));
    auto buses = s->waveforms.snapshot().traces[1];
    CHECK(buses.size() == 2 && buses.value(1) == sim::unknown(70));
    CHECK(same_as_snapshot());
    // back to the value before at the same time: no change is left
    s->set_input(bus, sim::floating(70));
    CHECK(s->waveforms.snapshot().traces[1].size() == 1);
    CHECK(same_as_snapshot());
    s->run_until(21 * sim::ms);
    s->set_input(bus, sim::known(1, 70));
    CHECK(s->waveforms.snapshot().traces[1].value(1) == sim::known(1, 70));
    CHECK(same_as_snapshot());

    s->waveforms.clear(*s);
    auto generation = s->waveforms.snapshot().generation;
    CHECK(s->waveforms.snapshot().traces[0].size() == 1);
    s->reset();
    data = s->waveforms.snapshot();
    CHECK(data.generation > generation && data.end == 0);
    CHECK(data.traces[0].size() == 1 && data.traces[0].at(0) == 0);
    CHECK(data.traces[0].value(0) == bit(false));
    CHECK(same_as_snapshot());

    // Retention remains bounded, with the earliest retained value usable.
    s->run_until((sim::waveform_recorder::max_samples + 20) * 500 * sim::us);
    data = s->waveforms.snapshot();
    CHECK(data.traces[0].size() == sim::waveform_recorder::max_samples);
    CHECK(data.traces[0].at(0) == 21 * 500 * sim::us);
    // the bus hasn't changed since, it still knows the start
    CHECK(data.begin == 0);
    CHECK(same_as_snapshot());
    // a mirror that falls behind further than the retention still catches up
    s->run_until((2 * sim::waveform_recorder::max_samples + 100) * 500 * sim::us);
    CHECK(same_as_snapshot());
    s->waveforms.remove(net);
    CHECK(s->waveforms.size() == 1);
    CHECK(same_as_snapshot());

    // Recording lives on the worker, survives pausing, and resumes on step.
    sim::runner r(make());
    r.query([net](sim::simulator& sim) { sim.waveforms.add(sim, net, "clk"); });
    r.wait_idle();
    r.set_speed(0);
    r.run(true);
    CHECK(eventually(r, [&] { return r.view().now() >= 10 * sim::ms; }));
    r.run(false);
    r.query([](sim::simulator&) {});
    r.wait_idle();
    auto paused = r.sim().waveforms.snapshot();
    CHECK(paused.traces[0].size() > 1);
    auto last_time = paused.end;
    // Readers can safely inspect the same history repeatedly while paused.
    CHECK(r.sim().waveforms.snapshot().end == last_time);
    r.step();
    r.wait_idle();
    CHECK(r.sim().waveforms.snapshot().end == last_time + 500 * sim::us);
    r.reset();
    r.wait_idle();
    CHECK(r.sim().waveforms.snapshot().traces[0].size() == 1);
}

// a handshake (valid and data out, ready in) and a bus with a handshake each
// way and an input
struct bundle_fixture : fixture {
    model::bundle_type* hs;
    model::bundle_type* bus;

    bundle_fixture()
    {
        auto& lib = *ws.find_library("work");
        hs = &ws.create_bundle(lib, "hs");
        hs->fields = {{"valid", "", "1", model::direction::output}, {"ready", "", "1", model::direction::input}, {"data", "", "8", model::direction::output}};
        bus = &ws.create_bundle(lib, "bus");
        model::bundle_field a, b;
        a.name = "a";
        a.bundle = hs->id;
        b.name = "b";
        b.bundle = hs->id;
        b.flipped = true;
        bus->fields = {a, b, {"irq", "", "1", model::direction::input}};
        ws.bundles_changed();
    }

    // a port of the bundle type, see fixture::port
    int bundle_port(model::direction dir, glm::ivec2 connect_at, model::bundle_type& t, bool flipped = false)
    {
        int k = port(dir, connect_at, ws.layout_of(t.id).width);
        top->ports[k].bundle = t.id;
        top->ports[k].flipped = flipped;
        return k;
    }

    static model::param_values params(model::bundle_type& t, std::string fields = {}, bool one = false, bool flipped = false)
    {
        model::param_values v{{"bundle", model::bundle_ref(t.id)}, {"flipped", flipped}};
        if (!fields.empty()) v[one ? "field" : "fields"] = fields;
        return v;
    }
};

static void print_problems(const sim::simulator& s)
{
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
}

static void test_bundles()
{
    using model::direction;

    // a breakout splits a bundle into its fields. the top's port is the
    // flipped view, so valid and data are its inputs: only they are driven.
    {
        bundle_fixture f;
        f.place("breakout", {0, 0}, f.params(*f.hs));
        CHECK(f.ws.interface_of(f.sch().instances[0]).ports->size() == 4);
        int in = f.bundle_port(direction::input, {0, 0}, *f.hs, true);
        int valid = f.port(direction::output, instance_pin(f, 0, 1));
        int ready = f.port(direction::input, instance_pin(f, 0, 2));
        int data = f.port(direction::output, instance_pin(f, 0, 3), 8);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        // bits: valid 0, ready 1, data 2..9
        s.set_input(in, sim::known(0xAB << 2 | 0b11, 10));
        CHECK(s.port_value(valid) == bit(true));
        CHECK(s.port_value(data) == sim::known(0xAB, 8));

        // the fields of a bundle are recorded as bits of its net
        int net = s.net_of_port(s.root(), in);
        s.waveforms.add(s, net, "in.data", 2, 8);
        s.waveforms.add(s, net, "in.valid", 0, 1);
        s.waveforms.add(s, net, "too wide", 4, 8);
        CHECK(s.waveforms.size() == 2 && s.waveforms.contains(net));
        auto rec = s.waveforms.snapshot();
        CHECK(rec.signals[0].width == 8 && rec.signals[0].key != net);
        CHECK(sim::waveform_recorder::net_of(rec.signals[0].key) == net);
        CHECK(rec.traces[0].value(0) == sim::known(0xAB, 8));
        s.set_input(in, sim::known(0x12 << 2 | 0b01, 10));
        s.waveforms.capture(s);
        rec = s.waveforms.snapshot();
        CHECK(rec.traces[0].value(rec.traces[0].size() - 1) == sim::known(0x12, 8));
        s.waveforms.remove(rec.signals[0].key);
        CHECK(s.waveforms.size() == 1);
        // ready comes from the other side, not from the bundle port
        s.set_input(ready, bit(false));
        CHECK(s.port_value(in).slice(1, 1) == bit(false));
        s.set_input(ready, bit(true));
        CHECK(s.port_value(in).slice(1, 1) == bit(true));
    }

    // a mirrored breakout builds a bundle from single signals: valid and
    // data go into it, ready comes out of it
    {
        bundle_fixture f;
        f.place("breakout", {100, 0}, f.params(*f.hs));
        f.sch().instances[0].xf.mirror = true;
        CHECK(instance_pin(f, 0, 0) == glm::ivec2(100, 0) && instance_pin(f, 0, 1).x < 100);
        int valid = f.port(direction::input, instance_pin(f, 0, 1));
        int ready = f.port(direction::output, instance_pin(f, 0, 2));
        int data = f.port(direction::input, instance_pin(f, 0, 3), 8);
        // the master's view: the outside drives only ready
        int out = f.bundle_port(direction::output, {100, 0}, *f.hs);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        s.set_input(valid, bit(true));
        s.set_input(data, sim::known(0xC3, 8));
        auto v = s.port_value(out);
        CHECK(v.slice(0, 1) == bit(true) && v.slice(2, 8) == sim::known(0xC3, 8));
        // the outside drives ready, 0 at the start
        CHECK(v.slice(1, 1) == sim::known(0, 1));
        s.set_input(out, sim::known(1 << 1, 10));
        CHECK(s.port_value(ready) == bit(true));
        // the port's input doesn't override valid and data
        CHECK(s.port_value(out) == sim::known(0xC3 << 2 | 0b11, 10));
    }

    // only the chosen fields get an end, a tap picks one field off the
    // bundle wire, also of a nested bundle
    {
        bundle_fixture f;
        f.place("breakout", {0, 0}, f.params(*f.bus, "b.data, irq"));
        f.place("tap", {0, 0}, f.params(*f.bus, "a.data", true));
        CHECK(f.ws.interface_of(f.sch().instances[0]).ports->size() == 3);
        CHECK(f.ws.check_instance(f.sch().instances[0]).empty());
        int in = f.bundle_port(direction::input, {0, 0}, *f.bus, true);
        int bdata = f.port(direction::output, instance_pin(f, 0, 1), 8);
        int irq = f.port(direction::output, instance_pin(f, 0, 2));
        int adata = f.port(direction::output, instance_pin(f, 1, 1), 8);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        // in the flipped view a.data is an input, b.data and irq are
        // driven by the bundle's other side, which nothing is
        auto v = sim::floating(21);
        v.insert(2, sim::known(0x5A, 8));
        v.insert(12, sim::known(0xFF, 8));
        v.insert(20, bit(true));
        s.set_input(in, v);
        CHECK(s.port_value(adata) == sim::known(0x5A, 8));
        CHECK(s.port_value(irq) == sim::floating(1));
        CHECK(s.port_value(bdata) == sim::floating(8));
    }

    // a breakout end of a nested bundle is a bundle port of that type,
    // breakouts chain
    {
        bundle_fixture f;
        f.place("breakout", {0, 0}, f.params(*f.bus, "b"));
        auto end = (*f.ws.interface_of(f.sch().instances[0]).ports)[1];
        CHECK(end.bundle == f.hs->id && end.flipped && end.width == 10);
        auto at = instance_pin(f, 0, 1);
        f.place("breakout", at, f.params(*f.hs, "valid", false, true));
        CHECK(f.ws.check_instance(f.sch().instances[1]).empty());
        int in = f.bundle_port(direction::input, {0, 0}, *f.bus);
        int valid = f.port(direction::output, instance_pin(f, 1, 1));

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        // b is flipped in bus, so b.valid (bit 10) is an input of the port
        s.set_input(in, sim::known(1 << 10, 21));
        CHECK(s.port_value(valid) == bit(true));
    }

    // a bundle through a subcircuit: the consumer taps data off its port
    {
        bundle_fixture f;
        auto& sub = f.new_cell("consumer");
        int m = f.bundle_port(direction::input, {0, 0}, *f.hs, true);
        f.place("tap", {0, 0}, f.params(*f.hs, "data", true));
        int d = f.port(direction::output, instance_pin(f, 0, 1), 8);
        sub.symbol.pins.push_back({sub.new_id(), sub.ports[m].id, {-20, 0}});
        sub.symbol.pins.push_back({sub.new_id(), sub.ports[d].id, {20, 0}});

        f.new_cell("top2");
        int in = f.bundle_port(direction::input, {-20, 0}, *f.hs, true);
        f.place_cell(sub.id, {0, 0});
        int out = f.port(direction::output, {20, 0}, 8);

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        s.set_input(in, sim::known(0x3C << 2, 10));
        CHECK(s.port_value(out) == sim::known(0x3C, 8));
        // the scope of the consumer sees the bundle
        auto child = s.root().child(0);
        CHECK(child && s.port_value(*child, m) == s.port_value(in));
    }

    // a bundle on plain bits of the same width is a type mismatch, the
    // bits still connect
    {
        bundle_fixture f;
        f.place("breakout", {0, 0}, f.params(*f.hs));
        int in = f.port(direction::input, {0, 0}, 10);
        int data = f.port(direction::output, instance_pin(f, 0, 3), 8);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().size() == 1 && s.problems()[0].find("type mismatch") != std::string::npos);
        s.set_input(in, sim::known(0x77 << 2, 10));
        CHECK(s.port_value(data) == sim::known(0x77, 8));
    }

    // a breakout without a type or with unknown fields says so, a changed
    // type rebuilds it
    {
        bundle_fixture f;
        f.place("breakout", {0, 0});
        CHECK(f.ws.check_instance(f.sch().instances[0]) == "choose a bundle type");
        f.place("breakout", {0, 100}, f.params(*f.hs, "valid, nope"));
        CHECK(f.ws.check_instance(f.sch().instances[1]).find("nope") != std::string::npos);
        f.place("tap", {0, 200}, f.params(*f.hs));
        CHECK(f.ws.check_instance(f.sch().instances[2]) == "choose a field");
        f.place("breakout", {0, 300}, f.params(*f.hs));
        CHECK(f.ws.interface_of(f.sch().instances[3]).ports->size() == 4);
        f.hs->fields.push_back({"last", "", "1", direction::input});
        f.ws.bundles_changed();
        CHECK(f.ws.interface_of(f.sch().instances[3]).ports->size() == 5);
        CHECK((*f.ws.interface_of(f.sch().instances[3]).ports)[0].width == 11);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        // only the parameter problems, as instance messages they aren't
        // simulation problems
        print_problems(s);
        CHECK(s.problems().empty());
    }

    // a bundle with a parameter: the breakout and the port use the same
    // values, the fields sit at their offsets for them
    {
        bundle_fixture f;
        auto& lib = *f.ws.find_library("work");
        auto& st = f.ws.create_bundle(lib, "st");
        st.params = {model::int_param("W", "W", 8, 1, 64), model::int_param("U", "U", 0, 0, 8)};
        st.fields = {{"valid", "", "1", direction::output}, {"data", "", "W", direction::output}, {"user", "", "U", direction::output},
            {"last", "", "1", direction::output}};
        auto v = f.params(st);
        v["args"] = std::string("W=16");
        f.place("breakout", {0, 0}, v);
        auto iface = f.ws.interface_of(f.sch().instances[0]);
        // valid, data, last: user has no bits
        CHECK(iface.ports->size() == 4 && (*iface.ports)[0].width == 18 && (*iface.ports)[2].width == 16);
        CHECK(((*iface.ports)[0].bundle_args == model::param_values{{"W", int64_t(16)}}));
        int in = f.port(direction::input, {0, 0}, 18);
        f.top->ports[in].bundle = st.id;
        f.top->ports[in].flipped = true;
        f.top->ports[in].bundle_args = {{"W", int64_t(16)}};
        int data = f.port(direction::output, instance_pin(f, 0, 2), 16);
        int last = f.port(direction::output, instance_pin(f, 0, 3));

        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        s.set_input(in, sim::known(uint64_t(1) << 17 | 0xBEEF << 1 | 1, 18));
        CHECK(s.port_value(data) == sim::known(0xBEEF, 16));
        CHECK(s.port_value(last) == bit(true));

        // other values on the port are another type
        f.top->ports[in].bundle_args = {{"W", int64_t(16)}, {"U", int64_t(0)}};
        CHECK(netlist::extract(f.ws, *f.top).problems.empty());
        f.top->ports[in].bundle_args = {{"W", int64_t(15)}, {"U", int64_t(1)}};
        auto nl = netlist::extract(f.ws, *f.top);
        CHECK(nl.problems.size() == 1 && nl.problems[0].find("type mismatch") != std::string::npos);

        // a bad value shows on the instance
        f.sch().instances[0].params["args"] = std::string("W=100");
        CHECK(f.ws.check_instance(f.sch().instances[0]).find("out of range") != std::string::npos);
    }
}

// a c model cell "sink" whose model takes a handshake: x_ready (always 1),
// x_valid, x_data and seen (data while valid, else 0), in that order
struct sink_component : sim::component {
    void eval(std::span<value> ports, sim::context&) override
    {
        ports[0] = sim::known(1, 1);
        bool valid = ports[1] == sim::known(1, 1);
        ports[3] = valid ? ports[2] : sim::known(0, 8);
    }
};

struct sink_factory : sim::component_factory {
    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override
    {
        auto m = std::get_if<model::c_model_impl>(&info.cell.impl);
        if (!m || m->model != "sink") return builtin::factory().create(info, error);
        return std::make_unique<sink_component>();
    }
};

static void test_bundle_groups()
{
    using model::direction;
    bundle_fixture f;
    auto& sink = f.ws.create_cell(*f.ws.find_library("work"), "sink", model::c_model_impl{"test", "sink", {}, {}, {}});
    auto leaf = [&](const char* name, direction d, int w) {
        auto& p = sink.add_port(d, {0, 0});
        p.name = name;
        p.width = w;
    };
    leaf("x_ready", direction::output, 1);
    leaf("x_valid", direction::input, 1);
    leaf("x_data", direction::input, 8);
    leaf("seen", direction::output, 8);

    // the sink is the other side of the handshake
    model::port_group g;
    g.name = "x";
    g.bundle = f.hs->id;
    g.flipped = true;
    g.side = direction::input;
    sink.port_groups()->push_back(g);
    CHECK(f.ws.apply_groups(sink).empty());
    sink.generate_symbol(model::pin_order::declaration);
    CHECK(sink.ports.size() == 2 && sink.ports[0].name == "x" && sink.ports[0].bundle == f.hs->id && sink.ports[0].width == 10);
    CHECK(sink.model_ports().size() == 4 && sink.check().empty());

    // the model sees its four ports, the schematic one bundle
    f.place_cell(sink.id, {0, 0});
    auto iface = f.ws.interface_of(f.sch().instances[0]);
    CHECK(iface.ports->size() == 2 && iface.model_ports->size() == 4);
    CHECK((*iface.model_ports)[0].of == 0 && (*iface.model_ports)[0].bit == 1);
    CHECK((*iface.model_ports)[2].bit == 2 && (*iface.model_ports)[3].of == 1);

    int in = f.bundle_port(direction::input, instance_pin(f, 0, 0), *f.hs, true);
    int seen = f.port(direction::output, instance_pin(f, 0, 1), 8);
    sink_factory factory;
    sim::simulator s(f.ws, *f.top, factory);
    print_problems(s);
    CHECK(s.problems().empty());
    // valid and data from the top, ready from the sink
    s.set_input(in, sim::known(0x5A << 2 | 1, 10));
    CHECK(s.port_value(seen) == sim::known(0x5A, 8));
    CHECK(s.port_value(in).slice(1, 1) == bit(true));
    s.set_input(in, sim::known(0x5A << 2, 10));
    CHECK(s.port_value(seen) == sim::known(0, 8));

    // a model port that doesn't fit leaves the group out
    sink.leaf_ports()->at(2).width = 4;
    auto problems = f.ws.apply_groups(sink);
    CHECK(problems.size() == 1 && problems[0].find("x_data") != std::string::npos);
    CHECK(sink.ports.size() == 4);
    sink.leaf_ports()->at(2).width = 8;
    CHECK(f.ws.apply_groups(sink).empty() && sink.ports.size() == 2);

    // without groups the model's ports come back
    sink.port_groups()->clear();
    f.ws.apply_groups(sink);
    CHECK(sink.ports.size() == 4 && sink.leaf_ports()->empty());
}

// one builtin part with a top cell port on each of its pins (but those in
// unconnected), by pin name
struct part_fixture : fixture {
    std::map<std::string, int> ports;
    std::unique_ptr<sim::simulator> s;

    part_fixture(const std::string& kind, model::param_values params = {}, std::vector<std::string> unconnected = {})
    {
        place(kind, {0, 0}, std::move(params));
        auto iface = ws.interface_of(sch().instances[0]);
        for (int k = 0; k < (int)iface.ports->size(); k++) {
            auto& p = (*iface.ports)[k];
            if (std::find(unconnected.begin(), unconnected.end(), p.name) != unconnected.end()) continue;
            ports[p.name] = port(p.dir == model::direction::output ? model::direction::output : model::direction::input, instance_pin(*this, 0, k), p.width);
        }
        s = std::make_unique<sim::simulator>(ws, *top, builtin::factory());
        print_problems(*s);
        CHECK(s->problems().empty());
    }

    void set(const std::string& name, value v) { s->set_input(ports.at(name), std::move(v)); }
    void set(const std::string& name, uint64_t v) { set(name, sim::known(v, top->ports[ports.at(name)].width)); }
    value get(const std::string& name) { return s->port_value(ports.at(name)); }
    uint64_t num(const std::string& name) { return get(name).to_u64(); }
    // a clock edge
    void tick()
    {
        set("clk", uint64_t(0));
        set("clk", uint64_t(1));
    }
};

static void test_arith_values()
{
    using namespace sim;
    // carries across words
    auto a = *parse_hex("FFFFFFFF_FFFFFFFF_FFFFFFFF", 100);
    auto r = add(a, known(1, 100));
    CHECK(r.v == *parse_hex("1_00000000_00000000_00000000", 100) && !r.carry);
    r = add(*parse_hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFF", 100), known(1, 100));
    CHECK(is_zero(r.v) && r.carry);
    r = sub(known(0, 100), known(1, 100));
    CHECK(r.v == *parse_hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFF", 100) && r.carry);
    CHECK(add(known(0xF, 4), known(1, 4)).carry && !add(known(0xE, 4), known(1, 4)).carry);

    // products and quotients
    CHECK(mul(known(0xFF, 8), known(0xFF, 8), false) == known(0xFE01, 16));
    CHECK(mul(known(0xFF, 8), known(0xFF, 8), true) == known(1, 16));
    CHECK(mul(known(0xFE, 8), known(3, 8), true) == known(0xFFFA, 16));
    auto big = mul(*parse_hex("1_00000000_00000001", 70), *parse_hex("1_00000000_00000001", 70), false);
    CHECK(big == *parse_hex("1_00000000_00000002_00000000_00000001", 140));
    auto d = divmod(known(100, 8), known(7, 8), false);
    CHECK(d.q == known(14, 8) && d.r == known(2, 8));
    d = divmod(known(0xF9, 8), known(2, 8), true); // -7 / 2
    CHECK(d.q == known(0xFD, 8) && d.r == known(0xFF, 8)); // -3 r -1
    d = divmod(*parse_hex("FFFFFFFF_FFFFFFFF_FFFFFFFF", 100), known(0x10, 100), false);
    CHECK(d.q == *parse_hex("FFFFFFF_FFFFFFFF_FFFFFFFF", 100) && d.r == known(0xF, 100));
    d = divmod(known(0x80, 8), known(0xFF, 8), true); // -128 / -1 wraps
    CHECK(d.q == known(0x80, 8) && is_zero(d.r));

    // shifts, also by the width and more
    CHECK(shl(known(0x81, 8), 1) == known(0x02, 8) && shl(known(0x81, 8), 8) == known(0, 8));
    CHECK(shr(known(0x81, 8), 7) == known(1, 8) && shr(known(0x81, 8), 20) == known(0, 8));
    CHECK(sar(known(0x81, 8), 1) == known(0xC0, 8) && sar(known(0x81, 8), 9) == known(0xFF, 8) && sar(known(0x41, 8), 1) == known(0x20, 8));
    CHECK(rotl(known(0x81, 8), 1) == known(0x03, 8) && rotr(known(0x81, 8), 9) == known(0xC0, 8));
    CHECK(shl(known(1, 100), 99) == *parse_hex("8_00000000_00000000_00000000", 100));

    CHECK(compare(known(0xFF, 8), known(1, 8), false) == 1 && compare(known(0xFF, 8), known(1, 8), true) == -1);
    CHECK(compare(known(5, 8), known(5, 8), true) == 0);
    CHECK(popcount(*parse_hex("FFFFFFFF_FFFFFFFF_F", 68)) == 68);
    CHECK(extend(known(0x80, 8), 12, extend_mode::sign) == known(0xF80, 12) && extend(known(0x80, 8), 12, extend_mode::zero) == known(0x80, 12));
    CHECK(extend(known(0x3, 2), 4, extend_mode::one) == known(0xF, 4) && extend(known(0xAB, 8), 4, extend_mode::sign) == known(0xB, 4));
    CHECK(negate(known(1, 8)) == known(0xFF, 8) && negate(known(0, 8)) == known(0, 8));
}

static void test_arith_parts()
{
    // adder with carries, the carry in not connected counts as 0
    {
        part_fixture f("addsub", {{"carry_in", true}});
        f.set("a", 200), f.set("b", 100);
        CHECK(f.num("y") == 44 && f.num("cout") == 1);
        f.set("cin", 1);
        CHECK(f.num("y") == 45);
        f.set("b", sim::unknown(8));
        CHECK(f.get("y") == sim::unknown(8) && f.get("cout") == sim::unknown(1));
    }
    // subtractor and the input-selected mode
    {
        part_fixture f("addsub", {{"mode", std::string("subtract")}});
        f.set("a", 5), f.set("b", 7);
        CHECK(f.num("y") == 254 && f.num("cout") == 1);
        part_fixture g("addsub", {{"mode", std::string("input")}, {"width", int64_t(100)}});
        g.set("a", 5), g.set("b", 7), g.set("sub", 0);
        CHECK(g.num("y") == 12);
        g.set("sub", 1);
        CHECK(g.get("y") == *sim::parse_hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFE", 100));
    }
    // multiplier: low half or all of it, signed
    {
        part_fixture f("multiplier", {{"output", std::string("full")}, {"signed", true}});
        f.set("a", 0xFF), f.set("b", 0xFF);
        CHECK(f.get("y") == sim::known(1, 16));
        part_fixture g("multiplier");
        g.set("a", 20), g.set("b", 20);
        CHECK(g.num("y") == 144);
    }
    // divider, dividing by 0 is unknown
    {
        part_fixture f("divider", {{"signed", true}});
        f.set("a", 0xF9), f.set("b", 2);
        CHECK(f.num("q") == 0xFD && f.num("r") == 0xFF);
        f.set("b", 0);
        CHECK(f.get("q") == sim::unknown(8) && f.get("r") == sim::unknown(8));
    }
    {
        part_fixture f("negator");
        f.set("in", 3);
        CHECK(f.num("out") == 0xFD);
    }
    {
        part_fixture f("comparator", {{"signed", true}});
        f.set("a", 0xFF), f.set("b", 1);
        CHECK(f.num("lt") == 1 && f.num("eq") == 0 && f.num("gt") == 0);
        f.set("b", 0xFF);
        CHECK(f.num("eq") == 1);
    }
    // shifter: the amount pin has the bits for 0..width-1
    {
        part_fixture f("shifter", {{"operation", std::string("arithmetic right")}});
        CHECK(f.top->ports[f.ports["amount"]].width == 3);
        f.set("in", 0x90), f.set("amount", 4);
        CHECK(f.num("out") == 0xF9);
        part_fixture g("shifter", {{"operation", std::string("rotate left")}, {"width", int64_t(12)}});
        CHECK(g.top->ports[g.ports["amount"]].width == 4);
        // 13 is 1 modulo 12
        g.set("in", 0x801), g.set("amount", 13);
        CHECK(g.num("out") == 0x003);
    }
    // extender: a floating input bit becomes x, an unknown sign extends as x
    {
        part_fixture f("extender", {{"in_width", int64_t(4)}, {"out_width", int64_t(8)}});
        f.set("in", 0x9);
        CHECK(f.num("out") == 0xF9);
        f.set("in", sim::value(4, 0x1, 0x8, 0));
        CHECK(f.get("out") == sim::value(8, 0x1, 0xF8, 0));
    }
}

static void test_counter_part()
{
    // up to 5, wrapping, carry at the end
    // en is wired to a top input here, which starts at 0
    part_fixture f("counter", {{"max", int64_t(5)}, {"width", int64_t(4)}});
    f.set("en", 1);
    CHECK(f.num("q") == 0);
    for (int k = 1; k <= 5; k++)
        f.tick();
    CHECK(f.num("q") == 5 && f.num("carry") == 1);
    f.tick();
    CHECK(f.num("q") == 0 && f.num("carry") == 0);
    // en 0 holds, load takes d, rst wins over load
    f.set("en", 0);
    f.tick();
    CHECK(f.num("q") == 0 && f.num("carry") == 0);
    f.set("d", 3), f.set("load", 1);
    f.tick();
    CHECK(f.num("q") == 3);
    f.set("rst", 1);
    f.tick();
    CHECK(f.num("q") == 0);
    f.set("rst", 0), f.set("load", 0), f.set("en", 1);
    f.set("en", sim::unknown(1));
    f.tick();
    CHECK(f.get("q") == sim::unknown(4));

    // down and stay
    part_fixture g("counter", {{"direction", std::string("down")}, {"at_end", std::string("stay")}, {"width", int64_t(3)}});
    g.set("en", 1), g.set("d", 2), g.set("load", 1);
    g.tick();
    g.set("load", 0);
    g.tick(), g.tick(), g.tick();
    CHECK(g.num("q") == 0 && g.num("carry") == 1);
    // down and wrap goes to all ones
    part_fixture h("counter", {{"direction", std::string("down")}, {"width", int64_t(3)}});
    h.set("en", 1);
    h.tick();
    CHECK(h.num("q") == 7);

    // a not connected enable counts, load and reset are off
    part_fixture free("counter", {{"width", int64_t(4)}}, {"en", "load", "rst"});
    free.tick(), free.tick();
    CHECK(free.num("q") == 2);

    // the count is saved and loaded with the state
    auto comp = h.s->component_of(h.s->root(), 0);
    std::vector<uint8_t> state;
    CHECK(comp && comp->save_state(state));
    h.tick();
    CHECK(h.num("q") == 6);
    CHECK(comp->load_state(state));
    comp->reset();
    CHECK(comp->load_state(state));
}

static void test_plexers()
{
    // mux with an enable
    {
        part_fixture f("mux", {{"select", int64_t(2)}, {"width", int64_t(8)}, {"enable", true}});
        for (int k = 0; k < 4; k++)
            f.set("in" + std::to_string(k), 10 + k);
        f.set("en", 1), f.set("sel", 2);
        CHECK(f.num("out") == 12);
        f.set("sel", sim::value(2, 0, 1, 0));
        CHECK(f.get("out") == sim::unknown(8));
        f.set("sel", 3), f.set("en", 0);
        CHECK(f.get("out") == sim::known(0, 8));
    }
    {
        part_fixture f("demux", {{"select", int64_t(1)}, {"width", int64_t(4)}});
        f.set("in", 9), f.set("sel", 1);
        CHECK(f.num("out0") == 0 && f.num("out1") == 9);
    }
    {
        part_fixture f("decoder", {{"select", int64_t(3)}});
        CHECK(!f.ports.count("in") && f.top->ports[f.ports["out7"]].width == 1);
        f.set("sel", 5);
        CHECK(f.num("out5") == 1 && f.num("out4") == 0 && f.num("out0") == 0);
        f.set("sel", sim::unknown(3));
        CHECK(f.get("out5") == sim::unknown(1));
    }
    {
        part_fixture f("decoder", {{"select", int64_t(3)}, {"outputs", std::string("bus")}});
        CHECK(!f.ports.count("out0") && f.top->ports[f.ports["out"]].width == 8);
        f.set("sel", 5);
        CHECK(f.get("out") == sim::known(0x20, 8));
        f.set("sel", sim::unknown(3));
        CHECK(f.get("out") == sim::unknown(8));
    }
    {
        // too many select bits for separate pins: a bus
        part_fixture f("decoder", {{"select", int64_t(8)}});
        CHECK(f.top->ports[f.ports["out"]].width == 256);
    }
    {
        part_fixture f("priority", {{"inputs", int64_t(10)}});
        CHECK(f.top->ports[f.ports["index"]].width == 4);
        f.set("in", 0x0A0);
        CHECK(f.num("index") == 7 && f.num("valid") == 1);
        f.set("in", 0);
        CHECK(f.num("valid") == 0);
    }
    {
        part_fixture f("popcount", {{"width", int64_t(8)}});
        CHECK(f.top->ports[f.ports["count"]].width == 4);
        f.set("in", 0xFF);
        CHECK(f.num("count") == 8);
    }
    {
        part_fixture f("bitselect", {{"width", int64_t(10)}, {"out", int64_t(4)}});
        CHECK(f.top->ports[f.ports["sel"]].width == 2);
        f.set("in", 0x2B5), f.set("sel", 1);
        CHECK(f.num("out") == 0xB);
        // the last group is cut by the bus, the rest is 0
        f.set("sel", 2);
        CHECK(f.get("out") == sim::known(0x2, 4));
    }
}

static void test_tunnels()
{
    using model::direction;
    auto tunnel = [](const char* name) { return model::param_values{{"name", std::string(name)}}; };

    // the same name connects, without a wire, at any width
    {
        fixture f;
        f.place("tunnel", {0, 0}, tunnel("data"));
        f.place("tunnel", {200, 100}, tunnel("data"));
        f.place("tunnel", {0, 100}, tunnel("other"));
        int in = f.port(direction::input, {0, 0}, 8);
        int out = f.port(direction::output, {200, 100}, 8);
        int alone = f.port(direction::output, {0, 100}, 8);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        s.set_input(in, sim::known(0x5A, 8));
        CHECK(s.port_value(out) == sim::known(0x5A, 8));
        CHECK(s.port_value(alone) == sim::floating(8));
        // the tunnels' pins take the width of the net
        auto nl = netlist::extract(f.ws, *f.top);
        CHECK(nl.nets[nl.instance_nets[0][0]].width == 8 && nl.instance_nets[0][0] == nl.instance_nets[1][0]);
    }

    // different widths on tunnels of one name are a width error
    {
        fixture f;
        f.place("tunnel", {0, 0}, tunnel("x"));
        f.place("tunnel", {200, 0}, tunnel("x"));
        f.port(direction::input, {0, 0}, 8);
        f.port(direction::output, {200, 0}, 4);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        CHECK(s.problems().size() == 1 && s.problems()[0].find("width mismatch") != std::string::npos);
    }

    // a bundle goes through tunnels with its type
    {
        bundle_fixture f;
        f.place("tunnel", {0, 0}, tunnel("bus"));
        f.place("tunnel", {200, 0}, tunnel("bus"));
        f.bundle_port(direction::input, {0, 0}, *f.hs, true);
        f.bundle_port(direction::output, {200, 0}, *f.hs, true);
        auto nl = netlist::extract(f.ws, *f.top);
        CHECK(nl.problems.empty() && nl.nets.size() == 1 && nl.nets[0].bundle == f.hs->id);
    }

    // tunnels connect only within their schematic
    {
        fixture f;
        auto& sub = f.new_cell("sub");
        f.place("tunnel", {0, 0}, tunnel("x"));
        int sub_in = f.port(direction::input, {0, 0});
        sub.symbol.pins.push_back({sub.new_id(), sub.ports[sub_in].id, {-20, 0}});

        f.new_cell("top2");
        f.place_cell(sub.id, {0, 0});
        f.place("tunnel", {100, 0}, tunnel("x"));
        int out = f.port(direction::output, {100, 0});
        int in = f.port(direction::input, {-20, 0});
        sim::simulator s(f.ws, *f.top, builtin::factory());
        print_problems(s);
        CHECK(s.problems().empty());
        s.set_input(in, bit(true));
        CHECK(s.port_value(out) == sim::floating(1));
    }

    // a tunnel without a name says so
    {
        fixture f;
        f.place("tunnel", {0, 0}, tunnel(""));
        CHECK(f.ws.check_instance(f.sch().instances[0]) == "give the tunnel a name");
    }
}

static void test_number_formats()
{
    using F = model::number_format;
    auto fmt = [](F::kind_t k, int frac = 0) { return F{k, frac}; };
    auto show = [&](uint64_t v, int w, F f) { return sim::format_number(sim::known(v, w), f); };
    auto parse = [&](const char* t, int w, F f) { return sim::parse_number(t, w, f); };

    // integers
    CHECK(show(0xAB, 8, fmt(F::hex)) == "0xAB");
    CHECK(show(0xFF, 8, fmt(F::unsigned_dec)) == "255" && show(0xFF, 8, fmt(F::signed_dec)) == "-1");
    CHECK(parse("-128", 8, fmt(F::signed_dec)) == sim::known(0x80, 8));
    CHECK(!parse("-129", 8, fmt(F::signed_dec)) && !parse("256", 8, fmt(F::unsigned_dec)) && !parse("-1", 8, fmt(F::unsigned_dec)));
    CHECK(parse("0b1010", 8, fmt(F::unsigned_dec)) == sim::known(10, 8) && parse("ff", 8, fmt(F::hex)) == sim::known(0xFF, 8));
    CHECK(parse("1_000", 16, fmt(F::unsigned_dec)) == sim::known(1000, 16) && parse("17", 8, fmt(F::octal)) == sim::known(15, 8));
    CHECK(!parse("12", 8, fmt(F::binary)) && !parse("", 8, fmt(F::hex)));
    auto big = parse("1267650600228229401496703205375", 100, fmt(F::unsigned_dec)); // 2^100 - 1
    CHECK(big && *big == *sim::parse_hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFF", 100));
    CHECK(sim::format_number(*big, fmt(F::unsigned_dec)) == "1267650600228229401496703205375");

    // fixed point, exact
    CHECK(show(0x14, 8, fmt(F::unsigned_fixed, 4)) == "1.25");
    CHECK(show(0xEC, 8, fmt(F::signed_fixed, 4)) == "-1.25");
    CHECK(show(0x80, 8, fmt(F::signed_fixed, 7)) == "-1" && show(1, 8, fmt(F::unsigned_fixed, 8)) == "0.00390625");
    CHECK(parse("-1.25", 8, fmt(F::signed_fixed, 4)) == sim::known(0xEC, 8));
    CHECK(parse("0.1", 8, fmt(F::unsigned_fixed, 4)) == sim::known(2, 8)); // 0.125 is the nearest step
    CHECK(!parse("8", 8, fmt(F::signed_fixed, 4)) && parse("7.9375", 8, fmt(F::signed_fixed, 4)) == sim::known(0x7F, 8));
    CHECK(show(uint64_t(1) << 63, 64, fmt(F::signed_fixed, 0)) == "-9223372036854775808");

    // floats
    CHECK(show(0x3C00, 16, fmt(F::ieee_float)) == "1" && show(0xC000, 16, fmt(F::ieee_float)) == "-2");
    CHECK(show(0x3FC00000, 32, fmt(F::ieee_float)) == "1.5" && show(0x3FB999999999999A, 64, fmt(F::ieee_float)) == "0.1");
    CHECK(show(0x7C00, 16, fmt(F::ieee_float)) == "inf" && show(0x0001, 16, fmt(F::ieee_float)) == "5.9604645e-08");
    CHECK(parse("1.5", 32, fmt(F::ieee_float)) == sim::known(0x3FC00000, 32) && parse("0.1", 64, fmt(F::ieee_float)) == sim::known(0x3FB999999999999A, 64));
    CHECK(parse("65504", 16, fmt(F::ieee_float)) == sim::known(0x7BFF, 16) && parse("1e6", 16, fmt(F::ieee_float)) == sim::known(0x7C00, 16));
    CHECK(parse("5.9604645e-08", 16, fmt(F::ieee_float)) == sim::known(1, 16));
    CHECK(show(0x3FC0, 16, fmt(F::bfloat16)) == "1.5" && parse("1.5", 16, fmt(F::bfloat16)) == sim::known(0x3FC0, 16));
    // floats only for their widths, other widths show bits
    CHECK(!parse("1", 8, fmt(F::ieee_float)) && show(0xAB, 8, fmt(F::ieee_float)) == "0xAB");

    // ascii, the first character is the top byte
    CHECK(show(0x4869, 16, fmt(F::ascii)) == "\"Hi\"" && show(0x0041, 16, fmt(F::ascii)) == "\"\\x00A\"");
    CHECK(parse("\"Hi\"", 16, fmt(F::ascii)) == sim::known(0x4869, 16) && parse("A", 16, fmt(F::ascii)) == sim::known(0x41, 16));
    CHECK(parse("\\x41\\x42", 16, fmt(F::ascii)) == sim::known(0x4142, 16) && !parse("abc", 16, fmt(F::ascii)));

    // unknown bits show as bits in every format
    CHECK(sim::format_number(sim::value(4, 0x1, 0x2, 0), fmt(F::signed_dec)) == sim::to_string(sim::value(4, 0x1, 0x2, 0)));
}

// a framebuffer: pixels written on the clock edge, its picture shared with
// the ui
static void test_framebuffer()
{
    fixture f;
    auto def = builtin::registry().find("framebuffer");
    CHECK(def != nullptr);
    if (!def) return;
    auto params_of = [&](int w, int h, const char* format, const char* color = "white") {
        return model::resolve_params(def->params(), {{"width", int64_t(w)}, {"height", int64_t(h)}, {"format", std::string(format)}, {"color", std::string(color)}});
    };

    // clk, we, rst, x, y, data
    auto iface = def->build(params_of(100, 37, "rgb888"));
    CHECK(iface.ports.size() == 6);
    if (iface.ports.size() != 6) return;
    CHECK(iface.ports[0].name == "clk" && iface.ports[5].name == "data");
    CHECK(iface.ports[3].width == 7 && iface.ports[4].width == 6 && iface.ports[5].width == 24);
    CHECK(def->build(params_of(1, 64, "grey8")).ports[5].width == 8);
    CHECK(def->build(params_of(1, 64, "grey8")).ports[3].width == 1);
    CHECK(def->build(params_of(1, 64, "grey8")).ports[4].width == 6);
    CHECK(def->build(params_of(64, 65, "mono")).ports[5].width == 1);
    CHECK(def->build(params_of(64, 65, "mono")).ports[4].width == 7);

    auto cell = f.ws.find_cell(model::builtin_cell_id("framebuffer"));
    std::string error;
    auto params = params_of(100, 37, "rgb888");
    auto fb = builtin::factory().create({*cell, params, {}}, error);
    CHECK(fb != nullptr);
    if (!fb) return;
    auto px = fb->pixels();
    CHECK(px && px->width == 100 && px->height == 37 && px->rgba.size() == 3700);
    if (!px) return;
    auto at = [&](int x, int y) { return px->rgba[size_t(y) * 100 + x]; };
    constexpr uint32_t black = 0xFF000000u;
    CHECK(at(0, 0) == black);

    std::vector<value> ports{bit(false), sim::floating(1), sim::floating(1), sim::known(3, 7), sim::known(2, 6), sim::known(0x112233, 24)};
    sim::context ctx(0, false, sim::never);
    auto clock = [&] {
        ports[0] = bit(false);
        fb->eval(ports, ctx);
        ports[0] = bit(true);
        fb->eval(ports, ctx);
    };
    // only on the rising edge, a floating we writes. bytes r, g, b, a
    fb->eval(ports, ctx);
    CHECK(at(3, 2) == black);
    auto v0 = px->version;
    clock();
    CHECK(at(3, 2) == 0xFF332211u);
    CHECK(px->version > v0);
    // staying high doesn't write again
    ports[5] = sim::known(0xFFFFFF, 24);
    fb->eval(ports, ctx);
    CHECK(at(3, 2) == 0xFF332211u);

    // we 0 or unknown doesn't write
    ports[1] = bit(false);
    clock();
    CHECK(at(3, 2) == 0xFF332211u);
    ports[1] = sim::unknown(1);
    clock();
    CHECK(at(3, 2) == 0xFF332211u);
    ports[1] = bit(true);

    // outside the picture or an unknown address: nothing
    v0 = px->version;
    ports[3] = sim::known(100, 7);
    clock();
    ports[3] = sim::known(5, 7);
    ports[4] = sim::known(37, 6);
    clock();
    ports[4] = sim::unknown(6);
    clock();
    CHECK(px->version == v0);

    // unknown data bits are marked
    ports[4] = sim::known(0, 6);
    ports[5] = sim::unknown(24);
    clock();
    CHECK(at(5, 0) == 0xFFFF00FFu);
    ports[5] = sim::known(0x0000FF, 24);
    ports[3] = sim::known(99, 7);
    ports[4] = sim::known(36, 6);
    clock();
    CHECK(at(99, 36) == 0xFFFF0000u);

    // rst at the edge clears, and doesn't write
    ports[2] = bit(true);
    clock();
    CHECK(at(3, 2) == black && at(5, 0) == black && at(99, 36) == black);
    ports[2] = bit(false);
    clock();
    CHECK(at(99, 36) == 0xFFFF0000u);
    auto items = [&] {
        std::vector<sim::inspect_item> out;
        fb->inspect(out);
        return out;
    }();
    CHECK(items.size() == 1 && items[0].v.to_u64() == 4);
    // the simulation's reset too
    fb->reset();
    CHECK(at(99, 36) == black);
    CHECK(fb->pixels() == px);

    // grey and mono
    auto grey_params = params_of(4, 4, "grey8");
    auto grey = builtin::factory().create({*cell, grey_params, {}}, error);
    std::vector<value> gp{bit(false), bit(true), bit(false), sim::known(1, 2), sim::known(1, 2), sim::known(0x80, 8)};
    grey->eval(gp, ctx);
    gp[0] = bit(true);
    grey->eval(gp, ctx);
    CHECK(grey->pixels()->rgba[5] == 0xFF808080u);

    auto mono_params = params_of(4, 4, "mono", "green");
    auto mono = builtin::factory().create({*cell, mono_params, {}}, error);
    std::vector<value> mp{bit(false), bit(true), bit(false), sim::known(2, 2), sim::known(0, 2), bit(true)};
    mono->eval(mp, ctx);
    mp[0] = bit(true);
    mono->eval(mp, ctx);
    CHECK(mono->pixels()->rgba[2] == 0xFF30FF30u);

    // the simulator lists it with its path, inside a subcircuit
    auto& screen = f.new_cell("screen");
    f.place("framebuffer", {0, 0});
    screen.generate_symbol();
    auto& outer = f.new_cell("outer");
    f.place("led", {0, -200});
    f.place_cell(screen.id, {0, 0});
    sim::simulator s(f.ws, outer, builtin::factory());
    CHECK(s.problems().empty());
    CHECK(s.displays().size() == 1);
    if (s.displays().size() == 1) {
        auto& d = s.displays()[0];
        CHECK(d.path == "U2/U1" && d.pixels && d.pixels->width == 64);
        auto inner = s.root().child(1);
        CHECK(inner && inner->components[0] == d.component);
    }
}

// random and lfsr: repeatable from the seed, stepped by the clock
static void test_random()
{
    fixture f;
    std::string error;
    auto make = [&](const char* kind, int width, int64_t seed) {
        auto def = builtin::registry().find(kind);
        auto params = model::resolve_params(def->params(), {{"width", int64_t(width)}, {"seed", seed}});
        return builtin::factory().create({*f.ws.find_cell(model::builtin_cell_id(kind)), params, {}}, error);
    };
    sim::context ctx(0, false, sim::never);
    // clk, en, rst, q
    auto clock = [&](sim::component& c, std::vector<value>& ports) {
        ports[0] = bit(false);
        c.eval(ports, ctx);
        ports[0] = bit(true);
        c.eval(ports, ctx);
        return ports[3];
    };
    auto pins = [](int width) { return std::vector<value>{bit(false), sim::floating(1), sim::floating(1), sim::floating(width)}; };

    // the same seed, the same numbers. the first is there after reset.
    auto a = make("random", 8, 1), b = make("random", 8, 1), c = make("random", 8, 2);
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    auto pa = pins(8), pb = pins(8), pc = pins(8);
    a->eval(pa, ctx);
    c->eval(pc, ctx);
    CHECK(pa[3].fully_known() && pa[3].width() == 8);
    auto first = pa[3];
    std::vector<value> seq;
    bool differs = false;
    for (int k = 0; k < 20; k++) {
        seq.push_back(clock(*a, pa));
        CHECK(clock(*b, pb) == seq.back());
        differs |= clock(*c, pc) != seq.back();
    }
    CHECK(differs);
    CHECK(std::any_of(seq.begin(), seq.end(), [&](auto& v) { return v != seq[0]; }));
    // the clock staying high doesn't step
    a->eval(pa, ctx);
    CHECK(pa[3] == seq.back());

    // en 0 holds, an unknown en makes q unknown, rst starts over
    pa[1] = bit(false);
    CHECK(clock(*a, pa) == seq.back());
    pa[1] = sim::unknown(1);
    CHECK(!clock(*a, pa).fully_known());
    pa[1] = bit(true);
    pa[2] = bit(true);
    CHECK(clock(*a, pa) == first);
    pa[2] = bit(false);
    CHECK(clock(*a, pa) == seq[0]);
    a->reset();
    a->eval(pa, ctx);
    CHECK(pa[3] == first);

    // every byte value turns up, the bits are balanced
    std::set<uint64_t> seen;
    int ones = 0;
    for (int k = 0; k < 4096; k++) {
        auto v = clock(*a, pa).to_u64();
        seen.insert(v);
        ones += std::popcount(v);
    }
    CHECK(seen.size() == 256);
    CHECK(ones > 4096 * 4 * 95 / 100 && ones < 4096 * 4 * 105 / 100);

    // wider than a word: each word its own number
    auto wide = make("random", 130, 7);
    auto pw = pins(130);
    wide->eval(pw, ctx);
    CHECK(pw[3].fully_known() && pw[3].width() == 130 && pw[3].bits(0) != pw[3].bits(1));

    // lfsr: all 2^n - 1 values but 0 before it repeats
    for (int w = 2; w <= 20; w++) {
        auto l = make("lfsr", w, 1);
        auto pl = pins(w);
        l->eval(pl, ctx);
        CHECK(pl[3] == sim::known(1, w));
        uint64_t period = 0;
        bool zero = false;
        do {
            auto v = clock(*l, pl);
            zero |= v.to_u64() == 0;
            period++;
        } while (pl[3] != sim::known(1, w) && period <= (uint64_t(1) << w));
        CHECK(!zero);
        if (period != (uint64_t(1) << w) - 1) std::printf("  lfsr width %d: period %llu\n", w, (unsigned long long)period);
        CHECK(period == (uint64_t(1) << w) - 1);
    }
    // seed: bits above the width ignored, 0 taken as 1
    auto l = make("lfsr", 4, 0x35);
    auto pl = pins(4);
    l->eval(pl, ctx);
    CHECK(pl[3] == sim::known(5, 4));
    l = make("lfsr", 4, 0x10);
    l->eval(pl, ctx);
    CHECK(pl[3] == sim::known(1, 4));
    // shifts left, bit 0 is bits 3 xor 2 (taps 4, 3)
    CHECK(clock(*l, pl) == sim::known(2, 4));
    CHECK(clock(*l, pl) == sim::known(4, 4));
    CHECK(clock(*l, pl) == sim::known(9, 4));
    CHECK(clock(*l, pl) == sim::known(3, 4));
    // own taps: bit numbers or a hex mask
    auto with_taps = [&](int width, const char* taps) {
        auto def = builtin::registry().find("lfsr");
        auto params = model::resolve_params(def->params(), {{"width", int64_t(width)}, {"taps", std::string(taps)}});
        return std::make_pair(def->check(params), builtin::factory().create({*f.ws.find_cell(model::builtin_cell_id("lfsr")), params, {}}, error));
    };
    auto period_of = [&](sim::component& c, int w) {
        auto p = pins(w);
        c.eval(p, ctx);
        uint64_t n = 0;
        do
            n++;
        while (clock(c, p) != sim::known(1, w) && n <= (uint64_t(1) << w));
        return n;
    };
    auto taps_of = [](sim::component& c) {
        std::vector<sim::inspect_item> items;
        c.inspect(items);
        return items.size() == 2 && items[1].name == "taps" ? items[1].v.to_u64() : 0;
    };
    for (auto t : {"4 3", "4,3", " 3, 4 ", "0xC", "0xc", ""}) {
        auto [problem, c] = with_taps(4, t);
        CHECK(problem.empty() && c && taps_of(*c) == 0xC && period_of(*c, 4) == 15);
    }
    // not maximal: 4 2 (x^4 + x^2 + 1) repeats after 6
    auto [none, short_cycle] = with_taps(4, "4 2");
    CHECK(none.empty() && taps_of(*short_cycle) == 0xA && period_of(*short_cycle, 4) == 6);
    CHECK(with_taps(8, "8 6 5 4").second && taps_of(*with_taps(8, "0xB8").second) == 0xB8);
    // mistakes: a problem, and the default taps are used
    for (auto t : {"9", "0", "8 x", "0x1FF", "0x", "0x0", "0xG1", "-3", "1234"}) {
        auto [problem, c] = with_taps(8, t);
        if (problem.empty()) std::printf("  taps \"%s\" accepted\n", t);
        CHECK(!problem.empty() && c && taps_of(*c) == 0xB8);
    }

    auto wide_l = make("lfsr", 64, -1);
    auto p64 = pins(64);
    wide_l->eval(p64, ctx);
    CHECK(p64[3] == sim::known(~uint64_t(0), 64));
    CHECK(clock(*wide_l, p64).fully_known());
}

// masters, a crossbar and rams on one clock. instance 0 is the clock, then
// the masters, the crossbar (if any) and the rams.
struct axi_bench : fixture {
    int clock = 0;
    std::vector<int> masters, rams;
    int xbar = -1;
    int mx = 2000;

    axi_bench(int n_masters, int n_rams, const std::string& map, bool lite = false, bool with_xbar = true)
    {
        model::param_values axi{{"protocol", std::string(lite ? "AXI4-Lite" : "AXI4")}};
        place("clock", {-200, 500}, {{"frequency", int64_t(1000000)}});
        for (int i = 0; i < n_masters; i++) {
            masters.push_back((int)sch().instances.size());
            place("axi_master", {0, -300 * i}, axi);
        }
        if (with_xbar) {
            xbar = (int)sch().instances.size();
            auto v = axi;
            v["masters"] = int64_t(n_masters);
            v["slaves"] = int64_t(n_rams);
            v["map"] = map;
            place("axi_crossbar", {600, 0}, v);
        }
        for (int j = 0; j < n_rams; j++) {
            rams.push_back((int)sch().instances.size());
            place("axi_ram", {1200, -300 * j}, axi);
        }

        // master i to s i, m j to ram j, or the master straight to the ram
        for (int i = 0; i < n_masters; i++)
            connect(instance_pin(*this, masters[i], 2), with_xbar ? instance_pin(*this, xbar, i) : instance_pin(*this, rams[i], 0));
        for (int j = 0; with_xbar && j < n_rams; j++)
            connect(instance_pin(*this, xbar, n_masters + 2 + j), instance_pin(*this, rams[j], 0));

        // the clock to every aclk
        auto clk = instance_pin(*this, clock, 0);
        for (int i : masters)
            connect(clk, instance_pin(*this, i, 0));
        if (with_xbar) connect(clk, instance_pin(*this, xbar, n_masters));
        for (int j : rams)
            connect(clk, instance_pin(*this, j, 1));
    }

    // an L-shaped route through its own column, wires only join at their ends
    void connect(glm::ivec2 a, glm::ivec2 b)
    {
        mx += 10;
        wire(a, {mx, a.y});
        wire({mx, a.y}, {mx, b.y});
        wire({mx, b.y}, b);
    }
};

static sim::value axi_item(sim::simulator& s, int instance, const std::string& name)
{
    for (auto& item : s.inspect(s.root(), instance))
        if (item.name == name) return item.v;
    std::printf("  no item %s\n", name.c_str());
    failures++;
    return {};
}

static void axi_start(sim::simulator& s, int master, const std::string& op, uint64_t addr, uint64_t data = 0, int len = 0, int size = -1)
{
    s.interact(s.root(), master, "addr", (int64_t)addr);
    s.interact(s.root(), master, "data", (int64_t)data);
    s.interact(s.root(), master, "len", len);
    s.interact(s.root(), master, "size", size < 0 ? 63 : size);
    s.interact(s.root(), master, op, 0);
}

// runs whole clock cycles until the masters are idle, false after max
static bool axi_wait(sim::simulator& s, const axi_bench& b, int max = 100, int* cycles = nullptr)
{
    for (int k = 1; k <= max; k++) {
        s.run_until(s.now() + sim::us);
        bool busy = false;
        for (int m : b.masters)
            busy |= axi_item(s, m, "busy") == sim::known(1, 1);
        if (!busy) {
            if (cycles) *cycles = k;
            return true;
        }
    }
    return false;
}

static std::vector<uint8_t> ram_bytes(sim::simulator& s, int ram, uint64_t at, size_t n)
{
    std::vector<uint8_t> r(n, 0xEE);
    auto c = s.component_of(s.root(), ram);
    if (!c || !c->debug() || !c->debug()->read_memory(0, at, r)) {
        std::printf("  can't read ram %d\n", ram);
        failures++;
    }
    return r;
}

static void test_axi()
{
    // two masters, two rams of 4 KiB at 0 and 0x1000
    {
        axi_bench b(2, 2, "0x0000/4K, 0x1000/4K");
        CHECK(netlist::extract(b.ws, *b.top).problems.empty());
        CHECK(b.ws.check_instance(b.sch().instances[b.xbar]).empty());
        sim::simulator s(b.ws, *b.top, builtin::factory());
        for (auto& p : s.problems())
            std::printf("  %s\n", p.c_str());
        CHECK(s.problems().empty());
        s.run_until(3 * sim::us);

        // different slaves at the same time take as long as one alone
        int both = 0, alone = 0;
        axi_start(s, b.masters[0], "write", 0x10, 0x11223344);
        axi_start(s, b.masters[1], "write", 0x1020, 0xAABBCCDD);
        CHECK(axi_wait(s, b, 100, &both));
        axi_start(s, b.masters[0], "write", 0x14, 0x55667788);
        CHECK(axi_wait(s, b, 100, &alone));
        CHECK(both == alone);
        CHECK(axi_item(s, b.masters[0], "done") == sim::known(2, 32) && axi_item(s, b.masters[1], "done") == sim::known(1, 32));
        CHECK(axi_item(s, b.masters[0], "resp") == sim::known(0, 2) && axi_item(s, b.masters[1], "resp") == sim::known(0, 2));
        CHECK((ram_bytes(s, b.rams[0], 0x10, 8) == std::vector<uint8_t>{0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55}));
        CHECK((ram_bytes(s, b.rams[1], 0x20, 4) == std::vector<uint8_t>{0xDD, 0xCC, 0xBB, 0xAA}));

        // read back across the crossbar
        axi_start(s, b.masters[0], "read", 0x1020);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[0], "rdata first") == sim::known(0xAABBCCDD, 32));
        CHECK(axi_item(s, b.masters[0], "beats") == sim::known(1, 9));

        // an incrementing burst of 4 and reading it back
        axi_start(s, b.masters[1], "write", 0x100, 0x1000, 3);
        CHECK(axi_wait(s, b));
        CHECK((ram_bytes(s, b.rams[0], 0x100, 16) ==
            std::vector<uint8_t>{0x00, 0x10, 0, 0, 0x01, 0x10, 0, 0, 0x02, 0x10, 0, 0, 0x03, 0x10, 0, 0}));
        axi_start(s, b.masters[1], "read", 0x100, 0, 3);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[1], "beats") == sim::known(4, 9));
        CHECK(axi_item(s, b.masters[1], "rdata first") == sim::known(0x1000, 32));
        CHECK(axi_item(s, b.masters[1], "rdata last") == sim::known(0x1003, 32));

        // both on the same slave: one waits for the other, both finish
        axi_start(s, b.masters[0], "write", 0x200, 0xA0A0A0A0, 1);
        axi_start(s, b.masters[1], "write", 0x300, 0xB0B0B0B0, 1);
        CHECK(axi_wait(s, b, 100, &both));
        CHECK(both > alone);
        CHECK(ram_bytes(s, b.rams[0], 0x204, 1)[0] == 0xA1 && ram_bytes(s, b.rams[0], 0x304, 1)[0] == 0xB1);

        // one byte in lane 1: only that byte changes
        axi_start(s, b.masters[0], "write", 0x201, 0x5A00, 0, 0);
        CHECK(axi_wait(s, b));
        CHECK((ram_bytes(s, b.rams[0], 0x200, 4) == std::vector<uint8_t>{0xA0, 0x5A, 0xA0, 0xA0}));

        // nothing at 0x8000: DECERR, a burst read gets all its beats
        axi_start(s, b.masters[0], "write", 0x8000, 1, 1);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[0], "resp") == sim::known(3, 2));
        axi_start(s, b.masters[1], "read", 0x8000, 0, 2);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[1], "resp") == sim::known(3, 2) && axi_item(s, b.masters[1], "beats") == sim::known(3, 9));
        // and the slaves still work
        axi_start(s, b.masters[1], "read", 0x14);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[1], "resp") == sim::known(0, 2) && axi_item(s, b.masters[1], "rdata first") == sim::known(0x55667788, 32));
    }

    // AXI4-Lite through a 1x3 crossbar with the default map: 3 slices of
    // 1 GiB (2^32 / 4)
    {
        axi_bench b(1, 3, "", true);
        CHECK(netlist::extract(b.ws, *b.top).problems.empty());
        sim::simulator s(b.ws, *b.top, builtin::factory());
        CHECK(s.problems().empty());
        s.run_until(3 * sim::us);
        axi_start(s, b.masters[0], "write", 0x80000008, 0xCAFEF00D);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[0], "resp") == sim::known(0, 2));
        CHECK((ram_bytes(s, b.rams[2], 8, 4) == std::vector<uint8_t>{0x0D, 0xF0, 0xFE, 0xCA}));
        axi_start(s, b.masters[0], "read", 0x80000008);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[0], "rdata first") == sim::known(0xCAFEF00D, 32));
        // the fourth slice has no slave
        axi_start(s, b.masters[0], "read", 0xC0000000);
        CHECK(axi_wait(s, b));
        CHECK(axi_item(s, b.masters[0], "resp") == sim::known(3, 2));
    }

    // a master straight on a ram, reset in the middle of a write
    {
        axi_bench b(1, 1, "", false, false);
        int rst = b.port(model::direction::input, instance_pin(b, b.masters[0], 1));
        int rst_ram = b.port(model::direction::input, instance_pin(b, b.rams[0], 2));
        sim::simulator s(b.ws, *b.top, builtin::factory());
        CHECK(s.problems().empty());
        s.set_input(rst, bit(true));
        s.set_input(rst_ram, bit(true));
        s.run_until(3 * sim::us);
        axi_start(s, b.masters[0], "write", 0x40, 0x12345678, 7);
        s.run_until(s.now() + 2 * sim::us);
        CHECK(axi_item(s, b.masters[0], "busy") == sim::known(1, 1));
        s.set_input(rst, bit(false));
        s.set_input(rst_ram, bit(false));
        s.run_until(s.now() + 2 * sim::us);
        CHECK(axi_item(s, b.masters[0], "busy") == sim::known(0, 1));
        CHECK(axi_item(s, b.rams[0], "writing") == sim::known(0, 1));
        s.set_input(rst, bit(true));
        s.set_input(rst_ram, bit(true));
        axi_start(s, b.masters[0], "write", 0x40, 0x12345678);
        CHECK(axi_wait(s, b));
        CHECK((ram_bytes(s, b.rams[0], 0x40, 4) == std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12}));
    }

    // a bad map is shown on the crossbar
    {
        axi_bench b(1, 2, "0x0000/8K, 0x1000/4K");
        CHECK(b.ws.check_instance(b.sch().instances[b.xbar]).find("overlap") != std::string::npos);
    }
}

int main()
{
    test_values();
    test_and_gate();
    test_gates();
    test_not_chain();
    test_oscillation();
    test_problems();
    test_conflict();
    test_register();
    test_counter();
    test_hierarchy();
    test_nested_state();
    test_cell_params();
    test_shorted_ports();
    test_splitter();
    test_splitter_params();
    test_bundles();
    test_bundle_groups();
    test_arith_values();
    test_arith_parts();
    test_counter_part();
    test_plexers();
    test_tunnels();
    test_number_formats();
    test_wide_values();
    test_wide_circuits();
    test_time();
    test_tristate_bus();
    test_inout();
    test_inspect_state();
    test_runner();
    test_waveforms();
    test_firmware();
    test_io_parts();
    test_framebuffer();
    test_random();
    test_axi();
    test_seg7_view();
    test_rom_ram();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
