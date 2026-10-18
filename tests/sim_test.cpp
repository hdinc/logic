// tests for the netlist and the simulator with the builtin primitives. no test
// framework, a failing check prints the location and the test exits with 1.

#include "../src/builtin/builtin.h"
#include "../src/sim/simulator.h"

#include <cstdio>

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

    fixture()
    {
        ws.add_builtin_library(builtin::registry());
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
    CHECK(sim::to_string(sim::known(1, 1), 1) == "1");
    CHECK(sim::to_string(sim::known(0xAB, 8), 8) == "0xAB");
    CHECK(sim::to_string({0b01, 0b10, 0}, 2) == "x1");
    CHECK(sim::to_string(sim::floating(1), 1) == "z");

    // undriven bits stay z, disagreeing drivers give x
    value a{0b0011, 0, 0b1100};
    value b{0b0001, 0, 0b0100};
    auto r = sim::resolve(std::vector<value>{a, b}, 4);
    CHECK(r.bits == 0b0001);
    CHECK(r.x == 0b0010);
    CHECK(r.z == 0b0100);

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

    // rising edge on the first tick
    s.tick();
    CHECK(s.port_value(q) == sim::known(0x42, 8));
    s.set_input(d, sim::known(0x10, 8));
    s.tick(); // falling
    CHECK(s.port_value(q) == sim::known(0x42, 8));
    s.tick(); // rising
    CHECK(s.port_value(q) == sim::known(0x10, 8));
    CHECK(s.ticks() == 3);

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
        s.tick();
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
    s.tick(); // rising edge for the first one only
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
        CHECK(s.port_value(bus) == (value{0x03, 0, 0xF0}));
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
        s2.set_input(swapped, value{0x01, 0, 0xFE}); // bit 0 of the swapped bus is bit 4 of the input, a 0
        CHECK(s2.port_value(in) == (value{0xA5 & ~0x10, 0x10, 0}));
        CHECK(s2.port_value(swapped) == (value{0x5A & ~0x01, 0x01, 0}));
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

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
