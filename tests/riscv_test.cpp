// tests for the risc-v parts: the riscv-tests isa suites (tests/riscv/isa,
// see tests/riscv/build.sh) on a hart with a flat memory. no test framework,
// a failing check prints the location and the test exits with 1.

#include "../src/io/firmware.h"
#include "../src/riscv/decode.h"
#include "../src/riscv/hart.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace logic;

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
    test_disassemble();
    test_isa(dir);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
