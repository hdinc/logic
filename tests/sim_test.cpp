// tests for the netlist and the simulator with the builtin primitives. no test
// framework, a failing check prints the location and the test exits with 1.

#include "../src/builtin/builder.h"
#include "../src/builtin/builtin.h"
#include "../src/io/firmware.h"
#include "../src/sim/runner.h"
#include "../src/sim/simulator.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
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
        hs->fields = {{"valid", "", 1, model::direction::output}, {"ready", "", 1, model::direction::input}, {"data", "", 8, model::direction::output}};
        bus = &ws.create_bundle(lib, "bus");
        model::bundle_field a, b;
        a.name = "a";
        a.bundle = hs->id;
        b.name = "b";
        b.bundle = hs->id;
        b.flipped = true;
        bus->fields = {a, b, {"irq", "", 1, model::direction::input}};
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
        f.hs->fields.push_back({"last", "", 1, direction::input});
        f.ws.bundles_changed();
        CHECK(f.ws.interface_of(f.sch().instances[3]).ports->size() == 5);
        CHECK((*f.ws.interface_of(f.sch().instances[3]).ports)[0].width == 11);
        sim::simulator s(f.ws, *f.top, builtin::factory());
        // only the parameter problems, as instance messages they aren't
        // simulation problems
        print_problems(s);
        CHECK(s.problems().empty());
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
    test_shorted_ports();
    test_splitter();
    test_splitter_params();
    test_bundles();
    test_bundle_groups();
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
    test_rom_ram();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
