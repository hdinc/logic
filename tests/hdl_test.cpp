// tests for hdl cells simulated with verilator. skipped (exit code 77) if
// verilator isn't installed.
// usage: hdl_test <folder with the test sources> <cache folder>

#include "../src/builtin/builtin.h"
#include "../src/hdl/manager.h"
#include "../src/plugin/registry.h"
#include "../src/sim/runner.h"
#include "../src/sim/simulator.h"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace logic;
using sim::value;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static fs::path sources, cache;

static value bit(bool b) { return sim::known(b, 1); }

struct fixture {
    model::workspace ws;
    plugin::registry plugins{ws};
    hdl::manager hdl{ws, plugins, cache};
    sim::factory_chain factory{{&builtin::factory(), &plugins, &hdl}};
    model::library* lib;
    model::cell* top;

    fixture()
    {
        ws.add_builtin_library(builtin::registry());
        lib = &ws.add_library("work");
        lib->dir = sources;
        ws.add_provider(plugins);
        ws.add_provider(hdl);
        top = &ws.create_cell(*lib, "top");
    }

    model::schematic& sch() { return *top->get_schematic(); }

    // a refreshed hdl cell of a test source
    model::cell& hdl_cell(const std::string& module, bool expect_ok = true)
    {
        model::hdl_impl h;
        h.language = model::hdl_language::systemverilog;
        h.sources = {module + ".sv"};
        h.top = module;
        auto& c = ws.create_cell(*lib, module, h);
        std::string error;
        bool ok = ws.refresh_interface(c, error);
        if (ok != expect_ok) {
            std::printf("refresh of %s: %s\n", module.c_str(), ok ? "worked" : error.c_str());
            failures++;
        }
        return c;
    }

    int place(model::id cell, glm::ivec2 pos, model::param_values params = {})
    {
        model::instance i;
        i.id = top->new_id();
        i.cell = cell;
        i.name = "U" + std::to_string(sch().instances.size() + 1);
        i.pos = pos;
        i.params = std::move(params);
        sch().instances.push_back(i);
        return (int)sch().instances.size() - 1;
    }

    glm::ivec2 pin(int instance, const std::string& port)
    {
        auto& inst = sch().instances[instance];
        auto iface = ws.interface_of(inst);
        for (auto& p : *iface.ports)
            if (p.name == port) return model::pin_position(inst, *iface.symbol->find_pin_of_port(p.id));
        std::printf("no port %s\n", port.c_str());
        failures++;
        return {};
    }

    int port(model::direction dir, glm::ivec2 at, int width = 1)
    {
        auto pos = at + glm::ivec2{dir == model::direction::input ? -20 : 20, 0};
        top->add_port(dir, pos).width = width;
        return (int)top->ports.size() - 1;
    }

    sim::simulator simulate()
    {
        sim::simulator s(ws, *top, factory);
        for (auto& p : s.problems())
            std::printf("  problem: %s\n", p.c_str());
        return s;
    }
};

static void test_messages()
{
    auto d = hdl::parse_verilator_messages("%Error: a.sv:5:16: syntax error, unexpected ';'\n"
                                           "%Warning-WIDTH: dir/b.v:7:3: width mismatch\n"
                                           "%Error: C:\\x\\c.sv:9: no column\n"
                                           "%Error: Cannot find file containing module: 'x'\n"
                                           "%Error: Exiting due to 3 error(s)\n"
                                           "unrelated\n");
    CHECK(d.size() == 4);
    if (d.size() != 4) return;
    CHECK(d[0].severity == "error" && d[0].file == "a.sv" && d[0].line == 5 && d[0].column == 16 && d[0].message == "syntax error, unexpected ';'");
    CHECK(d[1].severity == "warning" && d[1].file == "dir/b.v" && d[1].line == 7);
    CHECK(d[2].file == "C:\\x\\c.sv" && d[2].line == 9 && d[2].column == 0);
    CHECK(d[3].file.empty() && d[3].message.find("Cannot find") == 0);
}

static void test_describe()
{
    fixture f;
    auto& c = f.hdl_cell("counter");
    CHECK(c.ports.size() == 4);
    if (c.ports.size() != 4) return;
    CHECK(c.ports[0].name == "clk" && c.ports[0].dir == model::direction::input && c.ports[0].width == 1);
    CHECK(c.ports[3].name == "q" && c.ports[3].dir == model::direction::output && c.ports[3].width == 8);
    auto& params = *c.external_params();
    CHECK(params.size() == 1 && params[0].name == "WIDTH" && params[0].type == model::param_def::integer);
    CHECK(params[0].def == model::param_value(int64_t(8)));
    CHECK(c.check().empty());

    // other parameters give other ports
    model::instance i;
    i.cell = c.id;
    i.params = {{"WIDTH", int64_t(12)}};
    CHECK((*f.ws.interface_of(i).ports)[3].width == 12);
    CHECK(f.ws.check_instance(i).empty());

    auto& w = f.hdl_cell("wide");
    CHECK(w.ports.size() == 7);
    if (w.ports.size() == 7) {
        CHECK(w.ports[0].width == 100 && w.ports[2].width == 100);
        CHECK(w.ports[5].name == "bus" && w.ports[5].dir == model::direction::inout && w.ports[5].width == 4);
    }

    // errors point at the source
    auto& b = f.hdl_cell("broken", false);
    std::vector<model::port> ports;
    std::vector<model::param_def> defs;
    std::string error;
    std::vector<hdl::diagnostic> diagnostics;
    hdl::verilator_job j{std::get<model::hdl_impl>(b.impl), sources, {}, "broken"};
    CHECK(!hdl::verilator_describe(f.hdl.verilator(), j, ports, defs, error, &diagnostics));
    CHECK(error.find("broken.sv:5") != std::string::npos);
    CHECK(!diagnostics.empty() && diagnostics[0].line == 5);

    j.impl.top = "nothing";
    j.impl.sources = {"counter.sv"};
    CHECK(!hdl::verilator_describe(f.hdl.verilator(), j, ports, defs, error));
}

static void test_counter()
{
    fixture f;
    auto& c = f.hdl_cell("counter");
    int u = f.place(c.id, {0, 0});
    int u4 = f.place(c.id, {0, 300}, {{"WIDTH", int64_t(4)}});
    for (int k : {u, u4})
        f.place(model::builtin_cell_id("clock"), f.pin(k, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000)}});
    int rst = f.port(model::direction::input, f.pin(u, "rst"));
    int en = f.port(model::direction::input, f.pin(u, "en"));
    int q = f.port(model::direction::output, f.pin(u, "q"), 8);
    // the 4 bit one always counts
    f.place(model::builtin_cell_id("constant"), f.pin(u4, "en") - glm::ivec2{10, 0});
    f.place(model::builtin_cell_id("constant"), f.pin(u4, "rst") - glm::ivec2{10, 0}, {{"value", int64_t(0)}});
    int q4 = f.port(model::direction::output, f.pin(u4, "q"), 4);

    // two builds, one per width
    CHECK(f.hdl.missing_builds(*f.top).size() <= 2);

    auto s = f.simulate();
    CHECK(s.problems().empty());
    CHECK(f.hdl.missing_builds(*f.top).empty());

    s.set_input(en, bit(true));
    s.set_input(rst, bit(true));
    s.step();
    CHECK(s.port_value(q) == sim::known(0, 8));
    s.set_input(rst, bit(false));
    s.run_until(20 * sim::ms);
    // edges at 1.5, 2.5 ... 19.5 ms after the reset at 0.5 ms
    CHECK(s.port_value(q) == sim::known(19, 8));
    // 20 edges from 0 wrap at 16
    CHECK(s.port_value(q4) == sim::known(20 % 16, 4));

    s.set_input(en, bit(false));
    s.run_until(25 * sim::ms);
    CHECK(s.port_value(q) == sim::known(19, 8));

    // reset starts the verilated models over
    s.reset();
    s.set_input(rst, bit(false));
    s.set_input(en, bit(true));
    s.run_until(3 * sim::ms);
    CHECK(s.port_value(q4) == sim::known(3, 4));
}

static void test_wide()
{
    fixture f;
    auto& c = f.hdl_cell("wide");
    int u = f.place(c.id, {0, 0});
    int a = f.port(model::direction::input, f.pin(u, "a"), 100);
    int b = f.port(model::direction::input, f.pin(u, "b"), 100);
    int sum = f.port(model::direction::output, f.pin(u, "sum"), 100);
    int oe = f.port(model::direction::input, f.pin(u, "oe"));
    int d = f.port(model::direction::input, f.pin(u, "d"), 4);
    int bus = f.port(model::direction::inout, f.pin(u, "bus"), 4);
    int seen = f.port(model::direction::output, f.pin(u, "seen"), 4);
    auto s = f.simulate();
    CHECK(s.problems().empty());

    s.set_input(a, *sim::parse_hex("F_FFFFFFFF_FFFFFFFF_FFFFFFFF", 100));
    s.set_input(b, sim::known(1, 100));
    CHECK(s.port_value(sum) == sim::known(0, 100));
    s.set_input(a, *sim::parse_hex("1_00000000_FFFFFFFF_FFFFFFFF", 100));
    CHECK(s.port_value(sum) == *sim::parse_hex("1_00000001_00000000_00000000", 100));

    // the design drives the bus
    s.set_input(d, sim::known(0x5, 4));
    s.set_input(oe, bit(true));
    CHECK(s.port_value(bus) == sim::known(0x5, 4));
    CHECK(s.port_value(seen) == sim::known(0x5, 4));
    // it lets go and the top cell drives
    s.set_input(oe, bit(false));
    CHECK(s.port_value(bus) == sim::floating(4));
    s.set_input(bus, sim::known(0xA, 4));
    CHECK(s.port_value(seen) == sim::known(0xA, 4));
    // both: the bits that differ are unknown on the net
    s.set_input(oe, bit(true));
    CHECK(s.port_value(bus) == value(4, 0, 0xF, 0));
}

static void test_delay_and_finish()
{
    fixture f;
    auto& c = f.hdl_cell("delay");
    int u = f.place(c.id, {0, 0});
    int a = f.port(model::direction::input, f.pin(u, "a"));
    int y = f.port(model::direction::output, f.pin(u, "y"));
    int done = f.port(model::direction::output, f.pin(u, "done"));

    auto& fin = f.hdl_cell("finish");
    int u2 = f.place(fin.id, {0, 300});
    int t = f.port(model::direction::output, f.pin(u2, "t"), 8);

    auto s = f.simulate();
    CHECK(s.problems().empty());
    CHECK(s.port_value(y) == bit(false));
    CHECK(s.port_value(done) == bit(false));

    s.run_until(2 * sim::ns);
    s.set_input(a, bit(true));
    CHECK(s.port_value(y) == bit(false));
    // 5 ns later. the finish model has events every ns in between.
    s.run_until(6900 * sim::ps);
    CHECK(s.port_value(y) == bit(false));
    CHECK(s.next_event() == 7 * sim::ns);
    s.run_until(7 * sim::ns);
    CHECK(s.port_value(y) == bit(true));
    CHECK(s.port_value(t) == sim::known(7, 8));

    s.run_until(30 * sim::ns);
    CHECK(s.port_value(done) == bit(true));
    // finished at 10 ns, the outputs stay
    CHECK(s.port_value(t) == sim::known(10, 8));
    auto messages = f.plugins.take_messages();
    CHECK(std::any_of(messages.begin(), messages.end(), [](auto& m) { return m.find("$finish") != std::string::npos; }));
}

static void test_background_build()
{
    fixture f;
    auto& c = f.hdl_cell("counter");
    // a width nothing else builds, unless an earlier run did
    model::param_values values{{"WIDTH", int64_t(23)}};
    f.place(c.id, {0, 0}, values);
    auto missing = f.hdl.missing_builds(*f.top);
    if (!missing.empty()) {
        CHECK(missing.size() == 1 && missing[0].cell == c.id);
        f.hdl.build(c, missing[0].values);
        CHECK(f.hdl.busy());
    }
    for (int k = 0; k < 600 && f.hdl.busy(); k++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!f.hdl.busy());
    if (!missing.empty()) {
        auto st = f.hdl.status(c.id);
        CHECK(st.state == hdl::build_status::done);
        CHECK(!st.log.empty());
    }
    CHECK(f.hdl.missing_builds(*f.top).empty());
    CHECK(f.hdl.is_built(c, model::resolve_params(*c.external_params(), values)));

    // a failing build reports why
    auto& b = f.hdl_cell("broken", false);
    f.hdl.build(b, {});
    for (int k = 0; k < 600 && f.hdl.busy(); k++)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto st = f.hdl.status(b.id);
    CHECK(st.state == hdl::build_status::failed && st.error.find("broken.sv:5") != std::string::npos);
    auto messages = f.hdl.take_messages();
    CHECK(!messages.empty() && messages.back().find("failed") != std::string::npos);
}

static void test_runner()
{
    // a verilated model on the simulation thread
    fixture f;
    auto& c = f.hdl_cell("counter");
    int u = f.place(c.id, {0, 0}, {{"WIDTH", int64_t(32)}});
    f.place(model::builtin_cell_id("clock"), f.pin(u, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000000)}});
    f.place(model::builtin_cell_id("constant"), f.pin(u, "en") - glm::ivec2{10, 0});
    f.place(model::builtin_cell_id("constant"), f.pin(u, "rst") - glm::ivec2{10, 0}, {{"value", int64_t(0)}});
    int q = f.port(model::direction::output, f.pin(u, "q"), 32);

    sim::runner r(std::make_unique<sim::simulator>(f.ws, *f.top, f.factory));
    CHECK(r.sim().problems().empty());
    r.set_speed(0.0);
    r.run(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    r.run(false);
    r.post([](sim::simulator&) {});
    r.wait_idle();
    auto t = r.view().now();
    CHECK(t > 0);
    CHECK(r.view().port_value(q) == sim::known((t + 500 * sim::ns) / sim::us, 32));
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: hdl_test <test sources> <cache folder>\n");
        return 2;
    }
    sources = fs::absolute(argv[1]);
    cache = fs::absolute(argv[2]);

    test_messages();

    auto tools = hdl::find_verilator();
    if (!tools.ok()) {
        std::printf("skipped: %s\n", tools.error.c_str());
        return failures ? 1 : 77;
    }
    std::printf("using %s, %s\n", tools.version.c_str(), tools.cxx_version.c_str());

    test_describe();
    test_counter();
    test_wide();
    test_delay_and_finish();
    test_background_build();
    test_runner();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
