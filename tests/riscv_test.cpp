// tests for the risc-v parts: the riscv-tests isa suites (tests/riscv/isa,
// see tests/riscv/build.sh) on a hart with a flat memory. no test framework,
// a failing check prints the location and the test exits with 1.

#include "../src/builtin/builtin.h"
#include "../src/io/firmware.h"
#include "../src/io/storage.h"
#include "../src/model/bundle.h"
#include "../src/netlist/netlist.h"
#include "../src/riscv/decode.h"
#include "../src/riscv/hart.h"
#include "../src/sim/simulator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

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

// memory at base, and the address the test reports to
struct flat_memory : riscv::bus {
    uint32_t base = 0x80000000u;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(1 << 20);
    uint32_t tohost = 0;
    uint32_t reported = 0;

    bool inside(uint32_t addr, int size) const { return addr >= base && uint64_t(addr - base) + size <= bytes.size(); }

    riscv::result load(uint32_t addr, int size, uint32_t& value, riscv::access) override
    {
        if (!inside(addr, size)) return riscv::result::fault;
        value = 0;
        std::memcpy(&value, &bytes[addr - base], size);
        return riscv::result::ok;
    }
    riscv::result store(uint32_t addr, int size, uint32_t value) override
    {
        if (!inside(addr, size)) return riscv::result::fault;
        std::memcpy(&bytes[addr - base], &value, size);
        if (addr == tohost && value) reported = value;
        return riscv::result::ok;
    }
};

// runs a test until it writes tohost: 1 is a pass, else the failed test
// case is the value >> 1. 0 if it didn't finish.
static uint32_t run_isa_test(const std::filesystem::path& file, std::string& error)
{
    io::firmware f;
    if (!io::load_firmware(file, f, error)) return 0;
    flat_memory m;
    for (auto& [address, name] : f.symbols)
        if (name == "tohost") m.tohost = uint32_t(address);
    if (!m.tohost) {
        error = "no tohost symbol";
        return 0;
    }
    m.bytes = f.flatten(m.base, m.bytes.size(), 0);
    riscv::hart h(m);
    h.reset(uint32_t(f.entry));
    for (int k = 0; k < 1000000 && !m.reported; k++) {
        h.time = h.cycle / 10;
        h.step();
    }
    if (!m.reported) error = "timeout at pc " + std::to_string(h.pc);
    return m.reported;
}

static void test_isa(const std::filesystem::path& dir)
{
    // no pmp entries, the test needs some
    const char* skipped[] = {"rv32mi-p-pmpaddr"};
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir)) files.push_back(e.path());
    std::sort(files.begin(), files.end());
    int passed = 0;
    for (auto& file : files) {
        auto name = file.filename().string();
        if (std::find(std::begin(skipped), std::end(skipped), name) != std::end(skipped)) continue;
        std::string error;
        uint32_t r = run_isa_test(file, error);
        if (r != 1) {
            if (r)
                std::printf("%s: test case %u failed\n", name.c_str(), r >> 1);
            else
                std::printf("%s: %s\n", name.c_str(), error.c_str());
            failures++;
        } else {
            passed++;
        }
    }
    std::printf("isa tests: %d passed\n", passed);
    CHECK(passed >= 80);
}

// a schematic with the builtins: parts are placed and connected by their
// pins with L-shaped routes in columns of their own
struct bench {
    model::workspace ws;
    model::cell* top;
    int column = 3000;

    bench()
    {
        ws.add_builtin_library(builtin::registry());
        top = &ws.create_cell(ws.add_library("work"), "top");
    }

    model::schematic& sch() { return *top->get_schematic(); }

    int place(const std::string& kind, glm::ivec2 pos, model::param_values params = {})
    {
        model::instance i;
        i.id = top->new_id();
        i.cell = model::builtin_cell_id(kind);
        i.name = "U" + std::to_string(sch().instances.size() + 1);
        i.pos = pos;
        i.params = std::move(params);
        sch().instances.push_back(i);
        return (int)sch().instances.size() - 1;
    }

    // the pin of an instance's port, by name
    glm::ivec2 pin(int instance, const std::string& port)
    {
        auto& inst = sch().instances[instance];
        auto iface = ws.interface_of(inst);
        for (auto& p : *iface.ports)
            if (p.name == port) return model::pin_position(inst, *iface.symbol->find_pin_of_port(p.id));
        std::printf("  no port %s\n", port.c_str());
        failures++;
        return {};
    }

    void wire(glm::ivec2 a, glm::ivec2 b) { sch().wires.push_back({top->new_id(), a, b}); }

    void connect(int a, const std::string& pa, int b, const std::string& pb)
    {
        auto from = pin(a, pa), to = pin(b, pb);
        column += 10;
        wire(from, {column, from.y});
        wire({column, from.y}, {column, to.y});
        wire({column, to.y}, to);
    }
};

// a word of a part's first debug memory, none if it can't be read
static value ram_word(sim::simulator& s, int ram, uint64_t at)
{
    uint8_t b[4] = {};
    auto c = s.component_of(s.root(), ram);
    if (!c || !c->debug() || !c->debug()->read_memory(0, at, b)) return {};
    return sim::known(uint64_t(b[0]) | uint64_t(b[1]) << 8 | uint64_t(b[2]) << 16 | uint64_t(b[3]) << 24, 32);
}

// a cpu and a ram with the test, on a 100 MHz clock, or with a crossbar
// between them
struct cpu_bench : bench {
    int clock, cpu, ram, xbar = -1;

    cpu_bench(const std::filesystem::path& elf, model::param_values cpu_params = {}, bool crossbar = false)
    {
        model::param_values ram_params{{"size", int64_t(16)}, {"file", elf.string()}};
        if (cpu_params.count("DATA_W")) ram_params["DATA_W"] = cpu_params["DATA_W"];
        clock = place("clock", {0, 400}, {{"frequency", int64_t(100000000)}});
        cpu = place("rv32_cpu", {0, 0}, std::move(cpu_params));
        ram = place("axi_ram", {1600, 0}, ram_params);
        if (crossbar) {
            xbar = place("axi_crossbar", {800, 0}, {{"masters", int64_t(1)}, {"slaves", int64_t(1)}, {"map", std::string("0x80000000/64K")}});
            connect(cpu, "m", xbar, "s0");
            connect(xbar, "m0", ram, "s");
            connect(clock, "clk", xbar, "aclk");
        } else {
            connect(cpu, "m", ram, "s");
        }
        connect(clock, "clk", cpu, "aclk");
        connect(clock, "clk", ram, "aclk");
    }
};

// the isa tests through the cpu part, its caches and axi
static void test_cpu_isa(const std::filesystem::path& dir, const char* label, model::param_values params, std::vector<std::string> skipped = {})
{
    skipped.push_back("rv32mi-p-pmpaddr");
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (std::find(skipped.begin(), skipped.end(), e.path().filename().string()) == skipped.end()) files.push_back(e.path());
    // RISCV_TEST_ONLY=rv32si-p-dirty runs one test, for debugging
    if (auto* only = std::getenv("RISCV_TEST_ONLY")) std::erase_if(files, [&](auto& f) { return f.filename() != only; });
    std::sort(files.begin(), files.end());
    int passed = 0;
    uint64_t instructions = 0;
    for (auto& file : files) {
        io::firmware f;
        std::string error;
        if (!io::load_firmware(file, f, error)) {
            std::printf("%s: %s\n", file.c_str(), error.c_str());
            failures++;
            continue;
        }
        uint64_t tohost = 0;
        for (auto& [address, name] : f.symbols)
            if (name == "tohost") tohost = address;
        cpu_bench b(file, params);
        sim::simulator s(b.ws, *b.top, builtin::factory());
        for (auto& p : s.problems())
            std::printf("  %s\n", p.c_str());
        if (!s.problems().empty()) {
            failures++;
            return;
        }
        value r;
        for (int k = 0; k < 200; k++) {
            s.run_until(s.now() + 10 * sim::us);
            // the d-cache has it if the test wrote it there
            r = ram_word(s, b.cpu, tohost);
            if (!r.width()) r = ram_word(s, b.ram, tohost % 65536);
            if (r.width() && r.to_u64()) break;
        }
        uint64_t v = r.width() ? r.to_u64() : 0;
        for (auto& item : s.inspect(s.root(), b.cpu))
            if (item.name == "instret") instructions += item.v.to_u64();
        if (v == 1) {
            passed++;
        } else {
            std::printf("%s (%s): %s\n", file.filename().c_str(), label, v ? ("test case " + std::to_string(v >> 1) + " failed").c_str() : "timeout");
            if (std::getenv("RISCV_TEST_DUMP"))
                for (auto& item : s.inspect(s.root(), b.cpu))
                    std::printf("  %s = %s\n", item.name.c_str(), item.v.width() ? sim::to_string(item.v).c_str() : item.text.c_str());
            failures++;
        }
    }
    std::printf("isa tests through the cpu part (%s): %d passed, %llu instructions\n", label, passed, (unsigned long long)instructions);
    CHECK(passed == (int)files.size());
}

static uint64_t item_u64(sim::simulator& s, int instance, const std::string& name)
{
    for (auto& item : s.inspect(s.root(), instance))
        if (item.name == name) return item.v.to_u64();
    std::printf("  no item %s\n", name.c_str());
    failures++;
    return 0;
}

// a test through a crossbar, and what goes over the bus
static void test_cpu_bus(const std::filesystem::path& dir)
{
    auto file = dir / "rv32ui-p-add";
    io::firmware f;
    std::string error;
    CHECK(io::load_firmware(file, f, error));
    uint64_t tohost = 0;
    for (auto& [address, name] : f.symbols)
        if (name == "tohost") tohost = address;

    cpu_bench b(file, {}, true);
    CHECK(netlist::extract(b.ws, *b.top).problems.empty());
    sim::simulator s(b.ws, *b.top, builtin::factory());
    CHECK(s.problems().empty());
    // nothing runs before aclk rose twice, the first fetch is a line fill
    s.run_until(5 * sim::ns);
    CHECK(item_u64(s, b.cpu, "instret") == 0);
    value r;
    for (int k = 0; k < 100 && !(r.width() && r.to_u64()); k++) {
        s.run_until(s.now() + 10 * sim::us);
        r = ram_word(s, b.cpu, tohost);
    }
    CHECK(r == sim::known(1, 32));
    // the code is a few lines, each a burst: far fewer transactions than
    // instructions
    uint64_t transactions = item_u64(s, b.cpu, "bus transactions"), instret = item_u64(s, b.cpu, "instret");
    CHECK(transactions > 5 && transactions < 40);
    CHECK(instret > 400);
    // the core ran at 8 instructions per aclk cycle while it hit the
    // caches: the cycles are the instructions plus the waits
    uint64_t cycles = item_u64(s, b.cpu, "cycle");
    CHECK(cycles >= instret);
}

static std::string item_text(sim::simulator& s, int instance, const std::string& name)
{
    for (auto& item : s.inspect(s.root(), instance))
        if (item.name == name) return item.text;
    return {};
}

// what a uart sent so far, collected from its terminal stream
struct uart_output {
    std::shared_ptr<sim::byte_stream> stream;
    std::string text;

    uart_output(sim::simulator& s, int uart)
    {
        if (auto c = s.component_of(s.root(), uart)) stream = c->terminal();
        CHECK(stream);
    }
    const std::string& get()
    {
        if (stream) {
            std::lock_guard lk(stream->m);
            text += stream->bytes;
            stream->bytes.clear();
        }
        return text;
    }
};

// the parts of the linux example's memory map: uart 0x01000000 (plic source
// 1), clint 0x02000000, plic 0x03000000, ram 0x80000000
struct soc_bench : bench {
    int clock, cpu, xbar, uart, clint, plic, ram;

    explicit soc_bench(const std::filesystem::path& elf, int ram_bits = 16, std::string images = {}, model::param_values cpu_params = {}, int data_bits = 32,
        int64_t clock_hz = 100000000)
    {
        clock = place("clock", {0, 600}, {{"frequency", clock_hz}});
        model::param_values bus{{"DATA_W", int64_t(data_bits)}};
        cpu_params["DATA_W"] = int64_t(data_bits);
        cpu = place("rv32_cpu", {0, 0}, cpu_params);
        char map[128];
        std::snprintf(map, sizeof(map), "0x01000000/4K, 0x02000000/64K, 0x03000000/64M, 0x80000000/%d", 1 << ram_bits);
        xbar = place("axi_crossbar", {800, 0}, {{"masters", int64_t(1)}, {"slaves", int64_t(4)}, {"map", std::string(map)}, {"DATA_W", int64_t(data_bits)}});
        uart = place("uart16550", {1600, 0}, bus);
        clint = place("riscv_clint", {1600, -400}, bus);
        plic = place("riscv_plic", {1600, -800}, bus);
        ram = place("axi_ram", {1600, -1400}, {{"size", int64_t(ram_bits)}, {"file", elf.string()}, {"images", images}, {"DATA_W", int64_t(data_bits)}});
        connect(cpu, "m", xbar, "s0");
        connect(xbar, "m0", uart, "s");
        connect(xbar, "m1", clint, "s");
        connect(xbar, "m2", plic, "s");
        connect(xbar, "m3", ram, "s");
        connect(clint, "msip", cpu, "msip");
        connect(clint, "mtip", cpu, "mtip");
        connect(uart, "irq", plic, "irq1");
        connect(plic, "meip", cpu, "meip");
        connect(plic, "seip", cpu, "seip");
        for (int i : {cpu, xbar, uart, clint, plic, ram})
            connect(clock, "clk", i, "aclk");
    }
};

// the devices with interrupts, from tests/riscv/src/devices.c
static void test_devices(const std::filesystem::path& firmware)
{
    auto file = firmware / "devices.elf";
    io::firmware f;
    std::string error;
    CHECK(io::load_firmware(file, f, error));
    uint64_t tohost = 0;
    for (auto& [address, name] : f.symbols)
        if (name == "tohost") tohost = address;
    CHECK(tohost);

    soc_bench b(file);
    CHECK(netlist::extract(b.ws, *b.top).problems.empty());
    sim::simulator s(b.ws, *b.top, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());
    uart_output out(s, b.uart);
    auto run_until_screen = [&](const std::string& text) {
        for (int k = 0; k < 200; k++) {
            if (out.get().find(text) != std::string::npos) return true;
            s.run_until(s.now() + 10 * sim::us);
        }
        std::printf("  output: '%s'\n", out.get().c_str());
        return false;
    };
    CHECK(run_until_screen("hello\ntimer\nsoft\nready\n"));
    // the timer interrupt came after its 100 us
    CHECK(s.now() >= 100 * sim::us);
    for (char c : std::string("abc\n"))
        s.interact(s.root(), b.uart, "key", c);
    CHECK(run_until_screen("ready\nABC\nbye\n"));
    if (std::getenv("RISCV_TEST_DUMP"))
        for (int i : {b.uart, b.plic, b.cpu})
            for (auto& item : s.inspect(s.root(), i))
                std::printf("  %s = %s\n", item.name.c_str(), item.v.width() ? sim::to_string(item.v).c_str() : item.text.c_str());
    value r;
    for (int k = 0; k < 10 && !(r.width() && r.to_u64()); k++) {
        s.run_until(s.now() + 10 * sim::us);
        r = ram_word(s, b.cpu, tohost);
    }
    CHECK(r == sim::known(1, 32));
    // the cpu slept in wfi most of the time
    CHECK(item_text(s, b.cpu, "state") == "wfi");
}

// boots workspace/examples/riscv_linux until the shell's prompt, printing the
// console and the speed. only with LOGIC_RISCV_LINUX=<workspace folder>, it
// takes about 15 s.
static void test_linux_boot()
{
    auto* dir = std::getenv("LOGIC_RISCV_LINUX");
    if (!dir) return;
    model::workspace ws;
    ws.add_builtin_library(builtin::registry());
    for (auto& m : io::load_workspace(ws, dir))
        std::printf("  %s\n", m.c_str());
    model::cell* top = nullptr;
    if (auto* lib = ws.find_library("examples"))
        for (auto& c : lib->cells)
            if (c->name == "riscv_linux") top = c.get();
    CHECK(top);
    if (!top) return;
    sim::simulator s(ws, *top, builtin::factory());
    for (auto& p : s.problems())
        std::printf("  %s\n", p.c_str());
    CHECK(s.problems().empty());
    CHECK(s.terminals().size() == 1);
    if (s.terminals().empty()) return;
    auto stream = s.terminals()[0].stream;
    int cpu = -1;
    auto& sch = *top->get_schematic();
    for (int i = 0; i < (int)sch.instances.size(); i++)
        if (sch.instances[i].name == "CPU") cpu = i;

    std::string text;
    auto start = std::chrono::steady_clock::now();
    double limit = std::getenv("LOGIC_RISCV_LINUX_SECONDS") ? std::atof(std::getenv("LOGIC_RISCV_LINUX_SECONDS")) : 120;
    bool booted = false;
    double elapsed = 0;
    while (elapsed < limit && !booted) {
        s.run_until(s.now() + sim::ms);
        std::string bytes;
        {
            std::lock_guard lk(stream->m);
            bytes.swap(stream->bytes);
        }
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fflush(stdout);
        text += bytes;
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        booted = text.size() >= 2 && text.compare(text.size() - 2, 2, "# ") == 0;
    }
    uint64_t instret = item_u64(s, cpu, "instret");
    std::printf("\nlinux: %s after %.1f s, %.3f s simulated, %llu instructions, %.2f MIPS\n", booted ? "prompt" : "no prompt", elapsed,
        double(s.now()) / double(sim::seconds), (unsigned long long)instret, double(instret) / elapsed / 1e6);
    CHECK(booted);
    // idle: linux waits in wfi for the next key, the simulation runs on
    sim::time idle_from = s.now();
    auto idle_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - idle_start < std::chrono::seconds(3))
        s.run_until(s.now() + sim::ms);
    double idle_wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - idle_start).count();
    std::printf("linux idle: %.3f x real time\n", double(s.now() - idle_from) / double(sim::seconds) / idle_wall);
}

// a few instructions at 0x80000000 on a flat memory
static riscv::hart run_program(flat_memory& m, std::initializer_list<uint32_t> program, int steps)
{
    uint32_t at = 0;
    for (uint32_t inst : program) {
        std::memcpy(&m.bytes[at], &inst, 4);
        at += 4;
    }
    riscv::hart h(m);
    h.reset(m.base);
    for (int k = 0; k < steps; k++)
        h.step();
    return h;
}

static void test_lr_sc()
{
    // a0 = 0x80001000, a1 = 0x80001004: lr a0, sc to a1 fails and ends the
    // reservation, so the sc to a0 fails too
    flat_memory m;
    auto h = run_program(m,
        {
            0x80001537, // lui a0, 0x80001
            0x00450593, // addi a1, a0, 4
            0x100522af, // lr.w t0, (a0)
            0x18b5a32f, // sc.w t1, a1, (a1)
            0x18a523af, // sc.w t2, a0, (a0)
        },
        5);
    CHECK(h.x[6] == 1 && h.x[7] == 1);
    // without the failing sc in between the sc succeeds
    flat_memory m2;
    auto h2 = run_program(m2, {0x80001537, 0x00450593, 0x100522af, 0x18a523af}, 4);
    CHECK(h2.x[7] == 0);
}

static void test_mip_seip()
{
    // csrs mip, t0 (stip) while the plic's seip line is high doesn't set the
    // software seip bit: after the line drops mip has stip only
    flat_memory m;
    auto program = {
        0x02000293u, // li t0, 0x20
        0x3442a073u, // csrs mip, t0
        0x34402373u, // csrr t1, mip
    };
    uint32_t at = 0;
    for (uint32_t inst : program) {
        std::memcpy(&m.bytes[at], &inst, 4);
        at += 4;
    }
    riscv::hart h(m);
    h.reset(m.base);
    h.set_lines(riscv::ip::seip);
    h.step();
    h.step();
    h.set_lines(0);
    h.step();
    CHECK(h.x[6] == riscv::ip::stip);
}

static void test_disassemble()
{
    CHECK(riscv::disassemble(0x00150513, 0) == "addi a0, a0, 1");
    CHECK(riscv::disassemble(0x00b50463, 0x100) == "beq a0, a1, 0x108");
    CHECK(riscv::disassemble(0x00812083, 0) == "lw ra, 8(sp)");
    CHECK(riscv::disassemble(0x30529073, 0) == "csrrw zero, mtvec, t0");
    CHECK(riscv::disassemble(0x30200073, 0) == "mret");
    CHECK(riscv::disassemble(0x0ff0000f, 0) == "fence");
    CHECK(riscv::disassemble(0xffffffff, 0) == ".word 0xffffffff");
}

int main(int argc, char** argv)
{
    std::filesystem::path dir = argc > 1 ? argv[1] : "tests/riscv/isa";
    std::filesystem::path firmware = argc > 2 ? argv[2] : "tests/riscv/firmware";
    test_disassemble();
    test_lr_sc();
    test_mip_seip();
    test_isa(dir);
    test_cpu_bus(dir);
    test_devices(firmware);
    test_linux_boot();
    test_cpu_isa(dir, "caches", {});
    // tiny direct mapped caches with short lines: many misses and write backs
    test_cpu_isa(dir, "small caches", {{"icache", int64_t(1)}, {"dcache", int64_t(1)}, {"ways", int64_t(1)}, {"line", int64_t(16)}});
    // misaligned accesses are exceptions on uncached memory, ma_data has no
    // handler for them
    test_cpu_isa(dir, "uncached", {{"icache", int64_t(0)}, {"dcache", int64_t(0)}}, {"rv32ui-p-ma_data"});
    test_cpu_isa(dir, "64 bit bus", {{"DATA_W", int64_t(64)}});
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
