// tests for the c model plugins with the example plugin of the sdk.
// usage: plugin_test <logic_examples plugin> <bad plugin> <logic_chips plugin> <test rom folder>
//                    [<logic_avr plugin>, empty or missing without simavr]

#include "../sdk/include/logic_model.h"
#include "../src/builtin/builtin.h"
#include "../src/plugin/dynlib.h"
#include "../src/plugin/registry.h"
#include "../src/sim/runner.h"
#include "../src/sim/simulator.h"

#include <chrono>
#include <cstdio>
#include <fstream>
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

static fs::path examples_path, bad_path, chips_path, roms_path, avr_path;

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
    model::cell& model_cell(const std::string& model, const std::string& plugin = "logic_examples")
    {
        auto& c = ws.create_cell(*lib, model, model::c_model_impl{plugin, model, {}});
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
    CHECK(p && p->info->model_count == 4);
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
    model::param_values zero{{"baud", int64_t(0)}};
    auto comp = h.plugins.create({u, zero, {}}, error);
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

static void test_toy_cpu()
{
    // ldi 3; out; dec; jnz 2; out; halt
    auto dir = fs::temp_directory_path() / "logic_plugin_test_cpu";
    fs::create_directories(dir);
    {
        // a broken file, to check the error
        std::ofstream out(dir / "count.hex");
        out << ":0800000001030306050203FF00\n";
    }
    std::vector<uint8_t> program{0x01, 0x03, 0x03, 0x06, 0x05, 0x02, 0x03, 0xFF};
    {
        // written as binary, simplest
        std::ofstream out(dir / "count.bin", std::ios::binary);
        out.write((const char*)program.data(), (std::streamsize)program.size());
    }

    fixture f;
    f.lib->dir = dir;
    auto& c = f.model_cell("toy_cpu");
    CHECK(c.ports.size() == 3 && (*c.external_params())[0].name == "firmware");
    int u = f.place(c.id, {0, 0}, {{"firmware", std::string("count.bin")}});
    f.place(model::builtin_cell_id("clock"), f.pin(u, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000)}});
    int port = f.port(model::direction::output, f.pin(u, "port"), 8);
    int halted = f.port(model::direction::output, f.pin(u, "halted"));

    auto s = f.simulate();
    CHECK(s.problems().empty());
    auto comp = s.component_of(s.root(), u);
    auto dbg = comp ? comp->debug() : nullptr;
    CHECK(dbg != nullptr);
    if (!dbg) return;

    CHECK(dbg->pc() == 0u);
    std::string text;
    CHECK(dbg->disassemble(0, text) == 2 && text == "ldi 0x03");
    CHECK(dbg->disassemble(3, text) == 1 && text == "dec");
    CHECK(dbg->memories().size() == 1 && dbg->memories()[0].size == 256);
    uint8_t mem[8];
    CHECK(dbg->read_memory(0, 0, mem) && std::equal(mem, mem + 8, program.begin()));

    // stops before dec each time through the loop
    dbg->set_breakpoint(3, true);
    CHECK(dbg->breakpoints() == std::vector<uint64_t>{3});
    CHECK(!s.run_until(sim::seconds));
    CHECK(s.paused().find("breakpoint") != std::string::npos && s.paused().find("U1") == 0);
    CHECK(dbg->pc() == 3u);
    CHECK(s.port_value(port) == sim::known(3, 8));

    CHECK(!s.run_until(sim::seconds));
    CHECK(dbg->pc() == 3u);
    CHECK(s.port_value(port) == sim::known(2, 8));

    // one instruction: the dec
    dbg->step_instruction();
    CHECK(!s.run_until(sim::seconds));
    CHECK(s.paused().find("step") != std::string::npos);
    CHECK(dbg->pc() == 4u);

    dbg->set_breakpoint(3, false);
    CHECK(s.run_until(sim::seconds));
    CHECK(s.paused().empty());
    CHECK(s.port_value(halted) == bit(true));
    CHECK(s.port_value(port) == sim::known(0, 8));
    auto items = s.inspect(s.root(), u);
    CHECK(items.size() == 2 && items[0].name == "a" && items[0].v == sim::known(0, 8));

    // reset starts the program again from the file's contents
    s.reset();
    CHECK(dbg->pc() == 0u);
    CHECK(s.port_value(halted) == bit(false));

    // a broken file is reported
    f.sch().instances[u].params["firmware"] = std::string("count.hex");
    auto t = f.simulate();
    CHECK(t.problems().size() == 1 && t.problems()[0].find("count.hex") != std::string::npos);
    fs::remove_all(dir);
}

static void test_uart_to_terminal()
{
    // the plugin's transmitter into the builtin terminal
    fixture f;
    auto& c = f.model_cell("uart_tx");
    int u = f.place(c.id, {0, 0}, {{"baud", int64_t(1000)}});
    int data = f.port(model::direction::input, f.pin(u, "data"), 8);
    int send = f.port(model::direction::input, f.pin(u, "send"));
    int t = f.place(model::builtin_cell_id("terminal"), {300, 300}, {{"baud", int64_t(1000)}});
    auto tx = f.pin(u, "tx"), rx = f.pin(t, "rx");
    f.sch().wires.push_back({f.top->new_id(), tx, {tx.x, rx.y}});
    f.sch().wires.push_back({f.top->new_id(), {tx.x, rx.y}, rx});
    auto s = f.simulate();
    CHECK(s.problems().empty());

    for (char ch : std::string("ok")) {
        s.set_input(data, sim::known((uint8_t)ch, 8));
        s.set_input(send, bit(true));
        s.run_until(s.now() + 12 * sim::ms);
        s.set_input(send, bit(false));
    }
    auto items = s.inspect(s.root(), t);
    CHECK(items.size() == 1 && items[0].text == "ok");
}

// a model of a plugin used through the c abi only, with ports of up to 64
// bits, to run long programs fast
struct direct_model {
    std::unique_ptr<plugin::dynlib> lib;
    const logic_model_desc* desc = nullptr;
    void* inst = nullptr;
    std::vector<uint64_t> bits, x, z;
    std::vector<logic_value> ports;
    logic_context ctx{0, 0, LOGIC_NEVER, nullptr};

    direct_model(const fs::path& path, const char* name, std::vector<uint32_t> widths)
        : bits(widths.size())
        , x(widths.size())
        , z(widths.size(), ~uint64_t(0))
    {
        std::string error;
        lib = plugin::dynlib::open(path, error);
        auto entry = lib ? (logic_plugin_entry_fn)lib->symbol(LOGIC_PLUGIN_ENTRY_NAME) : nullptr;
        auto p = entry ? entry(LOGIC_MODEL_ABI_VERSION) : nullptr;
        for (uint32_t k = 0; p && k < p->model_count; k++)
            if (std::string(p->models[k].name) == name) desc = &p->models[k];
        CHECK(desc != nullptr);
        if (!desc) return;
        logic_params params{nullptr, 0};
        static const logic_host host{nullptr, nullptr, ""};
        inst = desc->create(&params, &host);
        CHECK(inst != nullptr);
        for (size_t k = 0; k < widths.size(); k++)
            ports.push_back({&bits[k], &x[k], &z[k], widths[k]});
    }

    ~direct_model()
    {
        if (inst) desc->destroy(inst);
    }

    void set(int port, uint64_t v)
    {
        bits[port] = v;
        x[port] = z[port] = 0;
    }

    // evals at now, then at the wake time it asks for if that comes before
    // until
    void eval_at(uint64_t now, uint64_t until)
    {
        ctx.now = now;
        ctx.woken = 0;
        desc->eval(inst, ports.data(), &ctx);
        if (ctx.wake != LOGIC_NEVER && ctx.wake < until) {
            ctx.now = ctx.wake;
            ctx.woken = 1;
            desc->eval(inst, ports.data(), &ctx);
        }
    }
};

static void test_m6502_functional()
{
    // klaus dormann's test through the pins, memory in the test
    std::vector<uint8_t> mem(0x10000);
    {
        std::ifstream in(roms_path / "6502_functional_test.bin", std::ios::binary);
        in.read((char*)mem.data(), (std::streamsize)mem.size());
        CHECK(in.gcount() == 0x10000);
    }
    // the reset vector to the start of the test
    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0x04;

    // clk, res_n, irq_n, nmi_n, rdy, a, d, rw, we, sync
    direct_model m(chips_path, "mos6502", {1, 1, 1, 1, 1, 16, 8, 1, 1, 1});
    if (!m.inst) return;
    m.desc->reset(m.inst);

    const uint64_t period = LOGIC_US;
    uint64_t t = 0;
    uint16_t last = 0;
    int same = 0;
    uint64_t cycles = 0;
    for (; cycles < 200000000 && same < 3; cycles++) {
        m.set(0, 1);
        m.eval_at(t, t + period / 2);

        // falling edge: the cpu takes the data of a read, the next address
        // and a write's data come after the hold time
        m.set(0, 0);
        if (m.bits[7]) m.set(6, mem[m.bits[5]]);
        else m.z[6] = ~uint64_t(0);
        m.eval_at(t + period / 2, t + period);
        if (m.bits[8]) mem[m.bits[5]] = (uint8_t)m.bits[6];
        t += period;

        if (m.bits[9]) {
            // jmp * is a trap: success or a failed test
            auto pc = (uint16_t)m.bits[5];
            same = pc == last ? same + 1 : 0;
            last = pc;
        }
    }
    std::printf("  6502 functional test: %llu cycles, stopped at %04X\n", (unsigned long long)cycles, last);
    CHECK(last == 0x3469);
}

static void test_m6502_schematic()
{
    // ldx #3; loop: txa; sta $0200,x; dex; bne loop; lda #$42; sta $0300; jmp *
    // in a 64k ram from a file, at f000 with the reset vector
    auto dir = fs::temp_directory_path() / "logic_plugin_test_6502";
    fs::create_directories(dir);
    std::vector<uint8_t> image(0x10000);
    std::vector<uint8_t> program{0xA2, 0x03, 0x8A, 0x9D, 0x00, 0x02, 0xCA, 0xD0, 0xF9, 0xA9, 0x42, 0x8D, 0x00, 0x03, 0x4C, 0x0E, 0xF0};
    std::copy(program.begin(), program.end(), image.begin() + 0xF000);
    image[0xFFFC] = 0x00;
    image[0xFFFD] = 0xF0;
    {
        std::ofstream out(dir / "program.bin", std::ios::binary);
        out.write((const char*)image.data(), (std::streamsize)image.size());
    }

    fixture f;
    f.lib->dir = dir;
    auto& c = f.model_cell("mos6502", "logic_chips");
    CHECK(c.ports.size() == 10);
    int cpu = f.place(c.id, {0, 0});
    int ram = f.place(model::builtin_cell_id("ram"), {600, 600},
        {{"addr_width", int64_t(16)}, {"file", std::string("program.bin")}});
    f.place(model::builtin_cell_id("clock"), f.pin(cpu, "clk") - glm::ivec2{10, 0}, {{"frequency", int64_t(1000000)}});
    // through a vertical lane at x, so wires only meet at their ends
    auto connect = [&](glm::ivec2 p, glm::ivec2 q, int x) {
        f.sch().wires.push_back({f.top->new_id(), p, {x, p.y}});
        f.sch().wires.push_back({f.top->new_id(), {x, p.y}, {x, q.y}});
        f.sch().wires.push_back({f.top->new_id(), {x, q.y}, q});
    };
    connect(f.pin(cpu, "a"), f.pin(ram, "addr"), 300);
    connect(f.pin(cpu, "we"), f.pin(ram, "we"), 320);
    connect(f.pin(cpu, "d"), f.pin(ram, "data"), 900);
    int sync = f.port(model::direction::output, f.pin(cpu, "sync"));

    auto s = f.simulate();
    CHECK(s.problems().empty());
    auto ram_dbg = s.component_of(s.root(), ram)->debug();
    auto comp = s.component_of(s.root(), cpu);
    auto dbg = comp ? comp->debug() : nullptr;
    CHECK(dbg != nullptr && ram_dbg != nullptr);
    if (!dbg || !ram_dbg) return;

    // stops at the opcode fetch of dex, before it runs
    dbg->set_breakpoint(0xF006, true);
    CHECK(!s.run_until(sim::ms));
    CHECK(s.paused().find("breakpoint") != std::string::npos);
    CHECK(dbg->pc() == 0xF006u);
    CHECK(s.port_value(sync) == bit(true));
    std::string text;
    CHECK(dbg->disassemble(0xF003, text) == 3 && text.find("STA") != std::string::npos);
    CHECK(dbg->disassemble(0xF000, text) == 2);
    auto items = s.inspect(s.root(), cpu);
    CHECK(items.size() == 7 && items[1].name == "x" && items[1].v == sim::known(3, 8));

    dbg->step_instruction();
    CHECK(!s.run_until(sim::ms));
    CHECK(s.paused().find("step") != std::string::npos);
    CHECK(dbg->pc() == 0xF007u);
    items = s.inspect(s.root(), cpu);
    CHECK(items[1].v == sim::known(2, 8));

    dbg->set_breakpoint(0xF006, false);
    CHECK(s.run_until(sim::ms));
    CHECK(dbg->pc() == 0xF00Eu);
    uint8_t written[4] = {};
    CHECK(ram_dbg->read_memory(0, 0x0201, written) && written[0] == 1 && written[1] == 2 && written[2] == 3);
    // nothing written at 0200 (x is never 0 there) or outside
    CHECK(ram_dbg->read_memory(0, 0x01FF, std::span(written, 2)) && written[0] == 0 && written[1] == 0);
    CHECK(ram_dbg->read_memory(0, 0x0300, std::span(written, 1)) && written[0] == 0x42);

    // on the simulation thread: running again goes on after a breakpoint
    sim::runner r(std::make_unique<sim::simulator>(f.ws, *f.top, f.factory));
    r.set_speed(0.0);
    r.post([cpu](sim::simulator& sim) { sim.component_of(sim.root(), cpu)->debug()->set_breakpoint(0xF006, true); });
    auto run_to_pause = [&] {
        r.run(true);
        for (int k = 0; k < 200 && r.running(); k++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        r.post([](sim::simulator&) {});
        r.wait_idle();
        return r.view().now();
    };
    auto first = run_to_pause();
    CHECK(r.view().paused().find("breakpoint") != std::string::npos);
    auto second = run_to_pause();
    CHECK(r.view().paused().find("breakpoint") != std::string::npos);
    CHECK(second > first);
    fs::remove_all(dir);
}

// avr instructions for hand written test programs (atmega328p io addresses)
namespace avr_asm {
const int pinb = 0x03, ddrb = 0x04, portb = 0x05, smcr = 0x33;
const int ucsr0a = 0xC0, ucsr0b = 0xC1, ubrr0l = 0xC4, udr0 = 0xC6;
uint16_t sbi(int a, int b) { return uint16_t(0x9A00 | (a << 3) | b); }
uint16_t cbi(int a, int b) { return uint16_t(0x9800 | (a << 3) | b); }
uint16_t sbic(int a, int b) { return uint16_t(0x9900 | (a << 3) | b); }
uint16_t sbis(int a, int b) { return uint16_t(0x9B00 | (a << 3) | b); }
uint16_t ldi(int d, int k) { return uint16_t(0xE000 | ((k & 0xF0) << 4) | ((d - 16) << 4) | (k & 0x0F)); }
uint16_t out(int a, int r) { return uint16_t(0xB800 | ((a & 0x30) << 5) | (r << 4) | (a & 0x0F)); }
uint16_t dec(int d) { return uint16_t(0x940A | (d << 4)); }
uint16_t sbrs(int r, int b) { return uint16_t(0xFE00 | (r << 4) | b); }
// relative jumps from word address at to word address to
uint16_t brne(int at, int to) { return uint16_t(0xF401 | (((to - at - 1) & 0x7F) << 3)); }
uint16_t rjmp(int at, int to) { return uint16_t(0xC000 | ((to - at - 1) & 0xFFF)); }
// two words each
std::vector<uint16_t> sts(int k, int r) { return {uint16_t(0x9200 | (r << 4)), uint16_t(k)}; }
std::vector<uint16_t> lds(int d, int k) { return {uint16_t(0x9000 | (d << 4)), uint16_t(k)}; }
const uint16_t sei = 0x9478, cli = 0x94F8, sleep = 0x9588;

// a program from words and lists of words, written as a binary file
struct program {
    std::vector<uint16_t> words;
    program& operator<<(uint16_t w)
    {
        words.push_back(w);
        return *this;
    }
    program& operator<<(const std::vector<uint16_t>& ws)
    {
        words.insert(words.end(), ws.begin(), ws.end());
        return *this;
    }
    int here() const { return (int)words.size(); }
    void write(const fs::path& p) const
    {
        std::ofstream o(p, std::ios::binary);
        for (auto w : words) {
            o.put(char(w & 0xFF));
            o.put(char(w >> 8));
        }
    }
};
}

// an atmega328p in a fixture, with the program in a temporary folder
struct avr_fixture : fixture {
    fs::path dir;
    int mcu = -1;

    explicit avr_fixture(const avr_asm::program& prog, const char* name = "avr")
    {
        dir = fs::temp_directory_path() / (std::string("logic_plugin_test_") + name);
        fs::create_directories(dir);
        prog.write(dir / "firmware.bin");
        lib->dir = dir;
        auto& c = model_cell("atmega328p", "logic_avr");
        mcu = place(c.id, {0, 0}, {{"firmware", std::string("firmware.bin")}});
    }
    ~avr_fixture() { fs::remove_all(dir); }

    // a top port on a pin of the mcu
    int at(const std::string& pin, model::direction dir) { return port(dir, this->pin(mcu, pin)); }
};

static void test_avr_blink()
{
    using namespace avr_asm;
    // sbi ddrb,5; ldi r24,0x20; loop: out pinb,r24; ldi r25,100; wait: dec r25; brne wait; rjmp loop
    // 1 + 1 + 99 * 3 + 2 + 2 = 303 cycles per toggle
    program p;
    p << sbi(ddrb, 5) << ldi(24, 0x20);
    int loop = p.here();
    p << out(pinb, 24) << ldi(25, 100);
    int wait = p.here();
    p << dec(25) << brne(p.here(), wait);
    p << rjmp(p.here(), loop);

    avr_fixture f(p, "avr_blink");
    int led = f.at("PB5", model::direction::output);
    auto s = f.simulate();
    CHECK(s.problems().empty());
    CHECK(s.port_value(led) == sim::floating(1));

    // the times it changes
    std::vector<sim::time> edges;
    auto last = s.port_value(led);
    while (s.now() < 200 * sim::us && s.step()) {
        auto v = s.port_value(led);
        if (v != last) edges.push_back(s.now());
        last = v;
    }
    CHECK(edges.size() > 5);
    // sbi (2 cycles) makes it an output at 0, out (1 cycle) toggles it
    if (edges.size() > 5) {
        const sim::time cycle = 62500;
        CHECK(edges[0] == 2 * cycle);
        CHECK(edges[1] == 4 * cycle);
        for (size_t k = 2; k + 1 < edges.size(); k++)
            CHECK(edges[k + 1] - edges[k] == 303 * cycle);
    }
}

static void test_avr_inputs()
{
    using namespace avr_asm;
    // pb1 follows pb0, with pb0's pull-up on
    program p;
    p << sbi(ddrb, 1) << sbi(portb, 0);
    int loop = p.here();
    p << sbis(pinb, 0) << cbi(portb, 1) << sbic(pinb, 0) << sbi(portb, 1);
    p << rjmp(p.here(), loop);

    avr_fixture f(p, "avr_inputs");
    int in = f.at("PB0", model::direction::input);
    int outp = f.at("PB1", model::direction::output);
    auto s = f.simulate();
    CHECK(s.problems().empty());

    s.set_input(in, sim::floating(1));
    s.run_until(100 * sim::us);
    CHECK(s.port_value(outp) == bit(true));
    s.set_input(in, bit(false));
    s.run_until(200 * sim::us);
    CHECK(s.port_value(outp) == bit(false));
    s.set_input(in, bit(true));
    s.run_until(300 * sim::us);
    CHECK(s.port_value(outp) == bit(true));
    // seen within run ahead (10 us) and a few cycles
    s.set_input(in, bit(false));
    s.run_until(s.now() + 12 * sim::us);
    CHECK(s.port_value(outp) == bit(false));
}

static void test_avr_serial()
{
    using namespace avr_asm;
    // 9600 baud at 16 MHz (ubrr 103), sends "Hi", then echoes what comes in
    program p;
    p << ldi(24, 103) << sts(ubrr0l, 24) << ldi(24, 0x18) << sts(ucsr0b, 24);
    p << ldi(24, 'H') << sts(udr0, 24);
    int empty = p.here();
    p << lds(25, ucsr0a) << sbrs(25, 5);
    p << rjmp(p.here(), empty);
    p << ldi(24, 'i') << sts(udr0, 24);
    int echo = p.here();
    p << lds(25, ucsr0a) << sbrs(25, 7);
    p << rjmp(p.here(), echo);
    p << lds(24, udr0) << sts(udr0, 24);
    p << rjmp(p.here(), echo);

    avr_fixture f(p, "avr_serial");
    int rx = f.at("PD0", model::direction::input);
    int t = f.place(model::builtin_cell_id("terminal"), {400, 600}, {{"baud", int64_t(9600)}});
    auto txd = f.pin(f.mcu, "PD1"), term = f.pin(t, "rx");
    f.sch().wires.push_back({f.top->new_id(), txd, {txd.x + 100, txd.y}});
    f.sch().wires.push_back({f.top->new_id(), {txd.x + 100, txd.y}, {txd.x + 100, term.y}});
    f.sch().wires.push_back({f.top->new_id(), {txd.x + 100, term.y}, term});
    auto s = f.simulate();
    CHECK(s.problems().empty());

    s.set_input(rx, bit(true));
    s.run_until(5 * sim::ms);
    auto items = s.inspect(s.root(), t);
    CHECK(items.size() == 1 && items[0].text == "Hi");

    // a byte into rxd at 9600 baud comes back
    const sim::time bit_time = sim::seconds / 9600;
    uint16_t frame = uint16_t(('!' << 1) | (1 << 9));
    for (int k = 0; k < 10; k++) {
        s.set_input(rx, bit((frame >> k) & 1));
        s.run_until(s.now() + bit_time);
    }
    s.run_until(s.now() + 3 * sim::ms);
    items = s.inspect(s.root(), t);
    CHECK(items.size() == 1 && items[0].text == "Hi!");
}

static void test_avr_debugger()
{
    using namespace avr_asm;
    program p;
    p << sbi(ddrb, 5) << ldi(24, 0x20);
    int loop = p.here();
    p << out(pinb, 24) << ldi(25, 100);
    int wait = p.here();
    p << dec(25) << brne(p.here(), wait);
    p << rjmp(p.here(), loop);

    avr_fixture f(p, "avr_debugger");
    auto s = f.simulate();
    auto dbg = s.component_of(s.root(), f.mcu)->debug();
    CHECK(dbg != nullptr);
    if (!dbg) return;

    auto mems = dbg->memories();
    CHECK(mems.size() == 3 && mems[0].size == 32768 && mems[1].size == 2304 && mems[2].size == 1024);
    uint8_t first[2];
    CHECK(dbg->read_memory(0, 0, first) && first[0] == 0x25 && first[1] == 0x9A);
    std::string text;
    CHECK(dbg->disassemble(0, text) == 2 && text == "sbi 0x04, 5");
    CHECK(dbg->disassemble(4, text) == 2 && text == "out 0x03, r24");
    CHECK(dbg->disassemble(10, text) == 2 && text == "brne 0x0008");

    // byte addresses: the out is at 4
    dbg->set_breakpoint(4, true);
    CHECK(!s.run_until(sim::ms));
    CHECK(s.paused().find("breakpoint") != std::string::npos);
    CHECK(dbg->pc() == 4u);
    dbg->step_instruction();
    CHECK(!s.run_until(sim::ms));
    CHECK(s.paused().find("step") != std::string::npos);
    CHECK(dbg->pc() == 6u);
    auto items = s.inspect(s.root(), f.mcu);
    CHECK(items.size() == 5 && items[0].name == "pc" && items[0].v == sim::known(6, 16));
    // the registers are the start of the data memory
    uint8_t r24 = 0;
    CHECK(dbg->read_memory(1, 24, std::span(&r24, 1)) && r24 == 0x20);

    // on the simulation thread, running goes on after the breakpoint
    sim::runner r(std::make_unique<sim::simulator>(f.ws, *f.top, f.factory));
    r.set_speed(0.0);
    int mcu = f.mcu;
    r.post([mcu](sim::simulator& sim) { sim.component_of(sim.root(), mcu)->debug()->set_breakpoint(4, true); });
    auto run_to_pause = [&] {
        r.run(true);
        for (int k = 0; k < 200 && r.running(); k++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        r.post([](sim::simulator&) {});
        r.wait_idle();
        return r.view().now();
    };
    auto a = run_to_pause();
    auto b = run_to_pause();
    CHECK(r.view().paused().find("breakpoint") != std::string::npos);
    CHECK(b > a);
}

static void test_avr_sleep()
{
    using namespace avr_asm;
    // sleeping without timers: nothing to wake it, no busy waking
    program p;
    p << ldi(24, 1) << out(smcr, 24) << sei;
    int again = p.here();
    p << sleep << rjmp(p.here(), again);

    avr_fixture f(p, "avr_sleep");
    auto s = f.simulate();
    s.run_until(sim::ms);
    CHECK(s.next_event() == sim::never);
    auto items = s.inspect(s.root(), f.mcu);
    CHECK(items.size() > 4 && items[4].name == "sleeping" && items[4].v == bit(true));

    // with interrupts off it stops for good
    program q;
    q << cli << sleep;
    avr_fixture g(q, "avr_sleep_cli");
    auto t = g.simulate();
    t.run_until(sim::ms);
    CHECK(t.next_event() == sim::never);
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::printf("usage: plugin_test <examples plugin> <bad plugin> <chips plugin> <test rom folder>\n");
        return 2;
    }
    examples_path = fs::absolute(argv[1]);
    bad_path = fs::absolute(argv[2]);
    chips_path = fs::absolute(argv[3]);
    roms_path = fs::absolute(argv[4]);
    if (argc > 5 && argv[5][0]) avr_path = fs::absolute(argv[5]);

    test_discovery();
    test_counter();
    test_wrap_and_wide_instances();
    test_seg7();
    test_uart_timing();
    test_errors();
    test_hot_reload();
    test_runner();
    test_toy_cpu();
    test_uart_to_terminal();
    test_m6502_functional();
    test_m6502_schematic();
    if (avr_path.empty()) {
        std::printf("  no avr plugin (simavr missing), avr tests skipped\n");
    } else {
        test_avr_blink();
        test_avr_inputs();
        test_avr_serial();
        test_avr_debugger();
        test_avr_sleep();
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
