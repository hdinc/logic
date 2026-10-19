// tests for the c model plugins with the example plugin of the sdk.
// usage: plugin_test <logic_examples plugin> <bad plugin>

#include "../src/builtin/builtin.h"
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

static fs::path examples_path, bad_path;

static value bit(bool b) { return sim::known(b, 1); }

// a workspace with the builtins, the plugins and a schematic to fill
struct fixture {
    model::workspace ws;
    plugin::registry plugins{ws};
    sim::factory_chain factory{{&builtin::factory(), &plugins}};
    model::library* lib;
    model::cell* top;

    fixture()
    {
        ws.add_builtin_library(builtin::registry());
        lib = &ws.add_library("work");
        lib->dir = examples_path.parent_path();
        plugins.search_dirs.push_back(examples_path.parent_path());
        ws.add_provider(plugins);
        top = &ws.create_cell(*lib, "top");
    }

    model::schematic& sch() { return *top->get_schematic(); }

    // a refreshed c model cell of the example plugin
    model::cell& model_cell(const std::string& model)
    {
        auto& c = ws.create_cell(*lib, model, model::c_model_impl{"logic_examples", model, {}});
        std::string error;
        if (!ws.refresh_interface(c, error)) {
            std::printf("refresh of %s failed: %s\n", model.c_str(), error.c_str());
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

    // pin of a port of a placed instance
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

    // a top cell port connected at the point
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

static void test_discovery()
{
    fixture f;
    auto found = f.plugins.scan();
    CHECK(std::any_of(found.begin(), found.end(), [](auto& a) { return a.name == "logic_examples"; }));
    CHECK(f.plugins.resolve("logic_examples", {}) == examples_path);
    CHECK(f.plugins.resolve(examples_path.filename().string(), examples_path.parent_path()) == examples_path);
    CHECK(f.plugins.resolve("nothing", {}).empty());

    std::string error;
    auto p = f.plugins.load("logic_examples", {}, error);
    CHECK(p && p->info->model_count == 3);
    CHECK(p && p->find("counter") && p->find("uart_tx") && !p->find("x"));
    // loaded once while unchanged
    CHECK(f.plugins.load("logic_examples", {}, error) == p);

    auto& c = f.model_cell("counter");
    CHECK(c.ports.size() == 4);
    CHECK(c.ports[3].name == "q" && c.ports[3].width == 8 && c.ports[3].dir == model::direction::output);
    CHECK(c.ports[0].description == "counts on the rising edge");
    CHECK(c.external_params()->size() == 1 && (*c.external_params())[0].name == "width");
    CHECK(c.check().empty());

    // another width through the instance parameters
    model::instance i;
    i.cell = c.id;
    i.params = {{"width", int64_t(20)}};
    CHECK((*f.ws.interface_of(i).ports)[3].width == 20);
    CHECK(f.ws.check_instance(i).empty());

    auto& u = f.model_cell("uart_tx");
    CHECK(u.ports.size() == 4 && (*u.external_params())[0].name == "baud");
}

static void test_counter()
{
    fixture f;
    auto& c = f.model_cell("counter");
    int u = f.place(c.id, {0, 0});
    // the builtin clock's output is 10 right of it
    f.place(model::builtin_cell_id("clock"), f.pin(u, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000)}});
    int rst = f.port(model::direction::input, f.pin(u, "rst"));
    int q = f.port(model::direction::output, f.pin(u, "q"), 8);

    auto s = f.simulate();
    CHECK(s.problems().empty());
    CHECK(s.port_value(q) == sim::known(0, 8));

    // en isn't connected: counts every rising edge
    s.step();
    CHECK(s.port_value(q) == sim::known(1, 8));
    s.run_until(10 * sim::ms);
    CHECK(s.port_value(q) == sim::known(10, 8));

    auto items = s.inspect(s.root(), u);
    CHECK(items.size() == 1 && items[0].name == "count" && items[0].v == sim::known(10, 8));

    s.set_input(rst, bit(true));
    s.step(); // the rising edge at 10.5 ms
    CHECK(s.port_value(q) == sim::known(0, 8));

    // x on rst at an edge makes the count unknown
    s.set_input(rst, sim::unknown(1));
    s.run_until(12 * sim::ms);
    CHECK(s.port_value(q) == sim::unknown(8));

    s.reset();
    CHECK(s.port_value(q) == sim::known(0, 8));
}

static void test_wrap_and_wide_instances()
{
    // a 4 bit counter wraps at 16
    fixture f;
    auto& c = f.model_cell("counter");
    int u = f.place(c.id, {0, 0}, {{"width", int64_t(4)}});
    f.place(model::builtin_cell_id("clock"), f.pin(u, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000)}});
    int q = f.port(model::direction::output, f.pin(u, "q"), 4);
    auto s = f.simulate();
    CHECK(s.problems().empty());
    s.run_until(17 * sim::ms);
    CHECK(s.port_value(q) == sim::known(17 % 16, 4));
}

static void test_seg7()
{
    fixture f;
    auto& c = f.model_cell("seg7_decoder");
    int u = f.place(c.id, {0, 0});
    int u2 = f.place(c.id, {0, 200}, {{"active_low", true}});
    int d = f.port(model::direction::input, f.pin(u, "digit"), 4);
    int seg = f.port(model::direction::output, f.pin(u, "seg"), 7);
    int d2 = f.port(model::direction::input, f.pin(u2, "digit"), 4);
    int seg2 = f.port(model::direction::output, f.pin(u2, "seg"), 7);
    auto s = f.simulate();
    CHECK(s.problems().empty());

    s.set_input(d, sim::known(8, 4));
    CHECK(s.port_value(seg) == sim::known(0x7F, 7));
    s.set_input(d, sim::known(1, 4));
    CHECK(s.port_value(seg) == sim::known(0x06, 7));
    s.set_input(d2, sim::known(1, 4));
    CHECK(s.port_value(seg2) == sim::known(0x79, 7));
    s.set_input(d, value(4, 0, 0b0100, 0));
    CHECK(s.port_value(seg) == sim::unknown(7));
}

static void test_uart_timing()
{
    // 1000 baud: one bit per ms
    fixture f;
    auto& c = f.model_cell("uart_tx");
    int u = f.place(c.id, {0, 0}, {{"baud", int64_t(1000)}});
    int data = f.port(model::direction::input, f.pin(u, "data"), 8);
    int send = f.port(model::direction::input, f.pin(u, "send"));
    int tx = f.port(model::direction::output, f.pin(u, "tx"));
    int busy = f.port(model::direction::output, f.pin(u, "busy"));
    auto s = f.simulate();
    CHECK(s.problems().empty());

    CHECK(s.port_value(tx) == bit(true));
    s.set_input(data, sim::known(0x35, 8));
    s.set_input(send, bit(true));
    CHECK(s.port_value(busy) == bit(true));
    CHECK(s.next_event() == sim::ms);

    // start bit, 0x35 from bit 0, stop bit, idle
    int expect[] = {0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1};
    for (int k = 0; k < 11; k++) {
        s.run_until(k * sim::ms + 500 * sim::us);
        if (s.port_value(tx) != bit(expect[k])) std::printf("  bit %d\n", k);
        CHECK(s.port_value(tx) == bit(expect[k]));
    }
    CHECK(s.port_value(busy) == bit(false));
    CHECK(s.next_event() == sim::never);
}

static void test_errors()
{
    fixture f;
    std::string error;

    // missing plugin and model
    auto& missing = f.ws.create_cell(*f.lib, "missing", model::c_model_impl{"no_such_plugin", "counter", {}});
    CHECK(!f.ws.refresh_interface(missing, error));
    CHECK(error.find("not found") != std::string::npos);
    auto& wrong = f.ws.create_cell(*f.lib, "wrong", model::c_model_impl{"logic_examples", "no_such_model", {}});
    CHECK(!f.ws.refresh_interface(wrong, error));
    CHECK(error.find("no model") != std::string::npos);

    // a plugin that doesn't support this abi, by path
    auto& bad = f.ws.create_cell(*f.lib, "bad", model::c_model_impl{bad_path.string(), "x", {}});
    CHECK(!f.ws.refresh_interface(bad, error));
    CHECK(error.find("abi") != std::string::npos);

    // placed, they are reported and the rest runs
    f.place(missing.id, {0, 0});
    auto s = f.simulate();
    CHECK(s.problems().size() == 1 && s.problems()[0].find("not found") != std::string::npos);

    // the plugin's ports changed since the refresh
    fixture g;
    auto& c = g.model_cell("counter");
    c.ports[3].width = 5;
    g.place(c.id, {0, 0});
    auto t = g.simulate();
    CHECK(t.problems().size() == 1 && t.problems()[0].find("refresh") != std::string::npos);

    // a model that refuses its parameters logs why
    fixture h;
    auto& u = h.model_cell("uart_tx");
    auto comp = h.plugins.create(u, {{"baud", int64_t(0)}}, error);
    CHECK(!comp && !error.empty());
    auto messages = h.plugins.take_messages();
    CHECK(messages.size() == 1 && messages[0].find("baud") != std::string::npos);
    CHECK(h.plugins.take_messages().empty());
}

static void test_hot_reload()
{
    auto dir = fs::temp_directory_path() / "logic_plugin_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto file = dir / ("hot" + examples_path.extension().string());
    fs::copy_file(examples_path, file);

    fixture f;
    std::string error;
    auto p1 = f.plugins.load(file.string(), {}, error);
    CHECK(p1 != nullptr);
    if (!p1) return;
    // the loaded file is a copy, the original can be replaced
    CHECK(p1->copy != file && fs::exists(p1->copy));

    fs::copy_file(examples_path, file, fs::copy_options::overwrite_existing);
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(2));
    auto p2 = f.plugins.load(file.string(), {}, error);
    CHECK(p2 && p2 != p1);
    // the old one still works for what uses it
    CHECK(p1->find("counter") != nullptr);

    // the old copy is removed when nothing uses it, the cache holds the new
    auto copy = p1->copy;
    p1.reset();
    CHECK(!fs::exists(copy));
    CHECK(fs::exists(p2->copy));
    fs::remove_all(dir);
}

static void test_runner()
{
    // a c model on the simulation thread
    fixture f;
    auto& c = f.model_cell("counter");
    int u = f.place(c.id, {0, 0}, {{"width", int64_t(32)}});
    f.place(model::builtin_cell_id("clock"), f.pin(u, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000000)}});
    int q = f.port(model::direction::output, f.pin(u, "q"), 32);

    sim::runner r(std::make_unique<sim::simulator>(f.ws, *f.top, f.factory));
    r.set_speed(0.0);
    r.run(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    r.run(false);
    r.post([](sim::simulator&) {});
    r.wait_idle();
    auto t = r.view().now();
    // a rising edge every us, the first at 0.5 us
    auto edges = (t + 500 * sim::ns) / sim::us;
    CHECK(t > 0);
    CHECK(r.view().port_value(q) == sim::known(edges, 32));
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: plugin_test <examples plugin> <bad plugin>\n");
        return 2;
    }
    examples_path = fs::absolute(argv[1]);
    bad_path = fs::absolute(argv[2]);

    test_discovery();
    test_counter();
    test_wrap_and_wide_instances();
    test_seg7();
    test_uart_timing();
    test_errors();
    test_hot_reload();
    test_runner();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
