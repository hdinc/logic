// the risc-v parts: an rv32ima cpu with caches on an axi4 master port. the
// hart is in src/riscv, after riscv-simulator by hdinc and uwrk9amtdn.

#include "axi_parts.h"
#include "builder.h"
#include "builtin.h"
#include "../riscv/hart.h"

#include <cstdio>
#include <cstring>
#include <set>

namespace logic::builtin {

namespace {

using axi::channel;
using axi::field;
using axi::high;
using axi::layout;
using axi::protocol;
using axi::set;
using axi::zeros;
using model::direction;
using sim::value;

std::string hex32(uint64_t v)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%08llX", (unsigned long long)v);
    return buf;
}

bool power_of_two(int64_t v) { return v > 0 && (v & (v - 1)) == 0; }

int log2_of(int64_t v)
{
    int r = 0;
    while ((int64_t(1) << r) < v)
        r++;
    return r;
}

// a value of n bytes from memory, little endian
value bytes_value(const uint8_t* p, int n)
{
    value v = zeros(8 * n);
    for (int k = 0; k < n; k += 8) {
        int chunk = std::min(8, n - k);
        uint64_t word = 0;
        std::memcpy(&word, p + k, chunk);
        v.insert(8 * k, sim::known(word, 8 * chunk));
    }
    return v;
}

// n bytes of the field at off in v into memory, unknown bits as 0
void value_bytes(const value& v, int off, uint8_t* p, int n)
{
    for (int k = 0; k < n; k += 8) {
        int chunk = std::min(8, n - k);
        uint64_t word = field(v, off + 8 * k, 8 * chunk);
        std::memcpy(p + k, &word, chunk);
    }
}

// a set associative cache with lru replacement
struct cache {
    struct line {
        uint32_t tag = 0;
        bool valid = false;
        bool dirty = false;
        uint64_t used = 0;
    };

    int line_bytes = 64;
    int sets = 0;
    int ways = 1;
    std::vector<line> lines;
    std::vector<uint8_t> data;
    uint64_t clock = 0;
    uint64_t hits = 0, misses = 0;

    cache(int bytes, int ways, int line_bytes)
        : line_bytes(line_bytes)
        , sets(bytes / (ways * line_bytes))
        , ways(ways)
        , lines(size_t(sets) * ways)
        , data(size_t(bytes))
    {
    }

    bool enabled() const { return sets > 0; }
    uint32_t set_of(uint32_t addr) const { return (addr / line_bytes) % sets; }
    uint32_t tag_of(uint32_t addr) const { return addr / line_bytes / sets; }

    // the line holding addr, -1 if none
    int find(uint32_t addr) const
    {
        uint32_t s = set_of(addr), t = tag_of(addr);
        for (int w = 0; w < ways; w++) {
            auto& l = lines[s * ways + w];
            if (l.valid && l.tag == t) return int(s * ways + w);
        }
        return -1;
    }
    // the line to replace for addr: an invalid one or the least recently
    // used
    int victim(uint32_t addr) const
    {
        uint32_t s = set_of(addr);
        int best = int(s * ways);
        for (int w = 0; w < ways; w++) {
            int k = int(s * ways + w);
            if (!lines[k].valid) return k;
            if (lines[k].used < lines[best].used) best = k;
        }
        return best;
    }
    uint8_t* bytes(int k) { return &data[size_t(k) * line_bytes]; }
    const uint8_t* bytes(int k) const { return &data[size_t(k) * line_bytes]; }
    // the address of line k's first byte
    uint32_t address(int k) const { return (lines[k].tag * uint32_t(sets) + uint32_t(k / ways)) * uint32_t(line_bytes); }
    void touch(int k) { lines[k].used = ++clock; }
    void install(int k, uint32_t addr, const uint8_t* from)
    {
        lines[k].tag = tag_of(addr);
        lines[k].valid = true;
        lines[k].dirty = false;
        std::memcpy(bytes(k), from, line_bytes);
        touch(k);
    }
    void invalidate()
    {
        for (auto& l : lines)
            l.valid = l.dirty = false;
    }
};

// what the hart waits for from the bus
struct request {
    enum class kind {
        none,
        fill_i,
        fill_d,
        writeback,
        read,
        write
    };
    kind k = kind::none;
    // the line's address for fills and write backs
    uint32_t addr = 0;
    int size = 0;
    uint32_t data = 0;
    // the cache line
    int line = -1;
    // for the prot signals
    bool privileged = false;
};

// a transaction the bus has done for the current instruction: uncached
// accesses with side effects and failed fills. the instruction's retry
// takes their results instead of accessing again.
struct completed {
    request::kind k;
    uint32_t addr;
    int size;
    uint32_t data;
    bool ok;
    // by the current try of the instruction
    bool taken = false;
};

// the hart's memory: the caches, uncached ranges, and the request for the
// bus when an access can't be done yet
class memory_system : public riscv::bus {
public:
    memory_system(int icache, int dcache, int ways, int line, std::vector<axi::range> cacheable)
        : ic(icache, ways, line)
        , dc(dcache, ways, line)
        , cacheable(std::move(cacheable))
    {
    }

    riscv::result load(uint32_t addr, int size, uint32_t& value, riscv::access kind) override
    {
        bool fetch = kind == riscv::access::execute;
        auto& c = fetch ? ic : dc;
        if (c.enabled() && is_cacheable(addr)) {
            uint8_t buf[4] = {};
            auto r = cached(c, fetch, addr, size, buf, false);
            if (r != riscv::result::ok) return r;
            value = 0;
            std::memcpy(&value, buf, size);
            return r;
        }
        if (!natural(addr, size)) return riscv::result::misaligned;
        return uncached(request::kind::read, addr, size, value);
    }

    riscv::result store(uint32_t addr, int size, uint32_t value) override
    {
        if (dc.enabled() && is_cacheable(addr)) {
            uint8_t buf[4];
            std::memcpy(buf, &value, 4);
            return cached(dc, false, addr, size, buf, true);
        }
        if (!natural(addr, size)) return riscv::result::misaligned;
        return uncached(request::kind::write, addr, size, value);
    }

    void fence_i() override { ic.invalidate(); }

    bool is_cacheable(uint32_t addr) const
    {
        for (auto& r : cacheable)
            if (addr >= r.base && addr - r.base < r.size) return true;
        return false;
    }

    // a byte as the caches have it, for debugging views
    std::optional<uint8_t> peek(uint32_t addr) const
    {
        for (auto* c : {&dc, &ic}) {
            if (!c->enabled()) continue;
            int k = c->find(addr);
            if (k >= 0) return c->bytes(k)[addr % c->line_bytes];
        }
        return std::nullopt;
    }

    void reset()
    {
        ic.invalidate();
        dc.invalidate();
        want = {};
        done.clear();
    }

    cache ic, dc;
    std::vector<axi::range> cacheable;
    // set by the access that stalled, taken by the bus unit
    request want;
    bool privileged = false;
    std::vector<completed> done;
    // before each try of an instruction
    void begin()
    {
        for (auto& c : done)
            c.taken = false;
    }

private:
    static bool natural(uint32_t addr, int size) { return power_of_two(size) && addr % uint32_t(size) == 0; }

    // a completed transaction of the instruction that isn't taken yet. the
    // retry may skip some, e.g. page table reads once the tlb has the page.
    const completed* replayed(request::kind k, uint32_t addr, int size)
    {
        for (auto& c : done)
            if (!c.taken && c.k == k && c.addr == addr && c.size == size) {
                c.taken = true;
                return &c;
            }
        return nullptr;
    }

    riscv::result uncached(request::kind k, uint32_t addr, int size, uint32_t& value)
    {
        if (auto* c = replayed(k, addr, size)) {
            if (k == request::kind::read) value = c->data;
            return c->ok ? riscv::result::ok : riscv::result::fault;
        }
        want = {k, addr, size, value, -1, privileged};
        return riscv::result::stall;
    }

    // size bytes at addr through the cache, they may span two lines
    riscv::result cached(cache& c, bool fetch, uint32_t addr, int size, uint8_t* buf, bool write)
    {
        uint32_t lb = uint32_t(c.line_bytes);
        int idx[2] = {-1, -1};
        uint32_t first = addr / lb, last = (addr + uint32_t(size) - 1) / lb;
        for (uint32_t n = first; n <= last; n++) {
            int k = c.find(n * lb);
            if (k < 0 && fetch) k = from_dcache(n * lb);
            if (k < 0) return miss(c, fetch, n * lb);
            idx[n - first] = k;
        }
        for (int i = 0; i < size; i++) {
            uint32_t a = addr + uint32_t(i);
            int k = idx[a / lb - first];
            uint8_t* p = c.bytes(k) + a % lb;
            if (write)
                *p = buf[i];
            else
                buf[i] = *p;
        }
        for (uint32_t n = first; n <= last; n++) {
            c.touch(idx[n - first]);
            if (write) c.lines[idx[n - first]].dirty = true;
        }
        c.hits++;
        return riscv::result::ok;
    }

    // the data cache has the newest copy of a line, e.g. of code that was
    // just written: an instruction fetch takes it from there. the line in
    // the instruction cache, -1 if the data cache doesn't have it.
    int from_dcache(uint32_t line_addr)
    {
        int k = dc.enabled() ? dc.find(line_addr) : -1;
        if (k < 0) return -1;
        int v = ic.victim(line_addr);
        ic.install(v, line_addr, dc.bytes(k));
        ic.misses++;
        return v;
    }

    riscv::result miss(cache& c, bool fetch, uint32_t line_addr)
    {
        // a fill that failed
        if (replayed(fetch ? request::kind::fill_i : request::kind::fill_d, line_addr, 0)) return riscv::result::fault;
        int v = c.victim(line_addr);
        if (fetch)
            want = {request::kind::fill_i, line_addr, 0, 0, v, privileged};
        else if (c.lines[v].valid && c.lines[v].dirty)
            want = {request::kind::writeback, c.address(v), 0, 0, v, privileged};
        else
            want = {request::kind::fill_d, line_addr, 0, 0, v, privileged};
        return riscv::result::stall;
    }
};

// ports: aclk, aresetn, msip, mtip, meip, seip, m
constexpr int p_clk = 0, p_rst = 1, p_msip = 2, p_mtip = 3, p_meip = 4, p_seip = 5, p_m = 6;

struct cpu_config {
    layout l;
    uint32_t reset_pc = 0x80000000u;
    uint32_t hartid = 0;
    uint32_t a1 = 0;
    int ratio = 8;
    sim::time run_ahead = sim::us;
    uint64_t timebase = 10000000;
    int icache = 16384, dcache = 16384, ways = 4, line = 64;
    std::vector<axi::range> cacheable;
};

class cpu_component : public sim::component, public sim::debug_interface {
public:
    explicit cpu_component(const cpu_config& c)
        : cfg(c)
        , l(c.l)
        , bus_bytes(c.l.wdata.w / 8)
        , mem(c.icache, c.dcache, c.ways, c.line, c.cacheable)
        , h(mem, c.hartid)
    {
        reset();
    }

    void reset() override
    {
        prev_clk = sim::bit_state::z;
        last_rise = sim::never;
        core_period = 0;
        restart();
    }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        auto now = ctx.now();
        auto clk = ports[p_clk].get(0);
        bool rising = prev_clk == sim::bit_state::zero && clk == sim::bit_state::one;
        prev_clk = clk;
        if (rising) {
            if (last_rise != sim::never && now > last_rise) core_period = std::max<sim::time>(1, (now - last_rise) / sim::time(cfg.ratio));
            last_rise = now;
            // not connected is not in reset
            if (ports[p_rst].get(0) == sim::bit_state::zero) {
                if (!in_reset) restart();
                in_reset = true;
            } else {
                in_reset = false;
                if (xfer.active) edge(ports[p_m], now);
            }
        }
        uint32_t lines = 0;
        if (ports[p_msip].get(0) == sim::bit_state::one) lines |= riscv::ip::msip;
        if (ports[p_mtip].get(0) == sim::bit_state::one) lines |= riscv::ip::mtip;
        if (ports[p_meip].get(0) == sim::bit_state::one) lines |= riscv::ip::meip;
        if (ports[p_seip].get(0) == sim::bit_state::one) lines |= riscv::ip::seip;
        h.set_lines(lines);
        if (!in_reset && paused_at != now) run(now, ctx);
        if (out_dirty) update_outputs();
        ports[p_m] = out;
    }

    void inspect(std::vector<sim::inspect_item>& out_items) const override
    {
        static const char* const modes[] = {"user", "supervisor", "?", "machine"};
        const char* state = in_reset ? "reset" : xfer.active || mem.want.k != request::kind::none ? "waiting for the bus" : h.waiting ? "wfi" : "running";
        out_items.push_back({"state", {}, state});
        out_items.push_back({"pc", sim::known(h.pc, 32), {}});
        out_items.push_back({"mode", {}, modes[h.priv & 3]});
        out_items.push_back({"instret", sim::known(h.instret, 64), {}});
        out_items.push_back({"cycle", sim::known(h.cycle, 64), {}});
        static const char* const names[32] = {"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
            "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
        for (int r = 1; r < 32; r++)
            out_items.push_back({std::string("x") + std::to_string(r) + " " + names[r], sim::known(h.x[r], 32), {}});
        for (uint32_t csr : {0x300u, 0x304u, 0x344u, 0x305u, 0x341u, 0x342u, 0x343u, 0x100u, 0x105u, 0x141u, 0x142u, 0x143u, 0x180u})
            out_items.push_back({riscv::csr_name(csr), sim::known(*h.read_csr(csr), 32), {}});
        auto rate = [](const cache& c) {
            uint64_t all = c.hits + c.misses;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.2f%% of %llu", all ? 100.0 * double(c.hits) / double(all) : 0.0, (unsigned long long)all);
            return std::string(buf);
        };
        if (mem.ic.enabled()) out_items.push_back({"i-cache hits", {}, rate(mem.ic)});
        if (mem.dc.enabled()) out_items.push_back({"d-cache hits", {}, rate(mem.dc)});
        out_items.push_back({"tlb misses", sim::known(h.tlb_misses, 64), {}});
        out_items.push_back({"bus transactions", sim::known(transactions, 64), {}});
    }

    sim::debug_interface* debug() override { return this; }

    std::vector<sim::memory_space> memories() const override { return {{"cached memory (physical)", uint64_t(1) << 32}}; }

    bool read_memory(int space, uint64_t address, std::span<uint8_t> out_bytes) const override
    {
        if (space != 0) return false;
        for (size_t k = 0; k < out_bytes.size(); k++) {
            auto b = mem.peek(uint32_t(address + k));
            if (!b) return false;
            out_bytes[k] = *b;
        }
        return true;
    }

    std::optional<uint64_t> pc() const override { return h.pc; }

    int disassemble(uint64_t address, std::string& text) const override
    {
        auto pa = h.debug_translate(uint32_t(address));
        if (!pa) return 0;
        uint32_t inst = 0;
        for (int k = 0; k < 4; k++) {
            auto b = mem.peek(*pa + uint32_t(k));
            if (!b) return 0;
            inst |= uint32_t(*b) << (8 * k);
        }
        text = riscv::disassemble(inst, uint32_t(address));
        return 4;
    }

    void set_breakpoint(uint64_t address, bool on) override
    {
        if (on)
            breaks.insert(uint32_t(address));
        else
            breaks.erase(uint32_t(address));
    }
    std::vector<uint64_t> breakpoints() const override { return {breaks.begin(), breaks.end()}; }
    void step_instruction() override { stepping = true; }

    riscv::hart& hart() { return h; }

private:
    // a transaction on the bus
    struct transfer {
        bool active = false;
        bool write = false;
        request req;
        int beats = 1;
        int size = 0;
        uint32_t addr = 0;
        bool addr_done = false;
        int beat = 0;
        bool error = false;
        std::vector<uint8_t> buf;
    };

    // the state after reset, also when aresetn is 0
    void restart()
    {
        h.reset(cfg.reset_pc);
        h.x[10] = cfg.hartid;
        h.x[11] = cfg.a1;
        mem.reset();
        xfer = {};
        t_core = 0;
        issue_at = 0;
        paused_at = sim::never;
        resume_pc.reset();
        stepping = false;
        out_dirty = true;
        transactions = 0;
    }

    uint64_t mtime_at(sim::time t) const
    {
        __extension__ typedef unsigned __int128 wide;
        return uint64_t(wide(t) * cfg.timebase / sim::seconds);
    }

    void run(sim::time now, sim::context& ctx)
    {
        ctx.wake_at(sim::never);
        if (xfer.active) return;
        if (mem.want.k != request::kind::none) {
            if (now < issue_at) {
                ctx.wake_at(issue_at);
                return;
            }
            start(now);
            return;
        }
        if (core_period == 0) return;
        if (t_core < now && (h.waiting || resumed)) t_core = now;
        resumed = false;
        sim::time limit = now + cfg.run_ahead;
        while (t_core <= limit) {
            if (!h.in_progress() && !breaks.empty() && breaks.count(h.pc) && resume_pc != h.pc) {
                ctx.request_pause("breakpoint at " + hex32(h.pc));
                resume_pc = h.pc;
                paused_at = now;
                return;
            }
            mem.begin();
            mem.privileged = h.priv != 0;
            h.time = mtime_at(t_core);
            auto r = h.step();
            if (r == riscv::step_result::stalled) {
                issue_at = t_core;
                if (now >= issue_at)
                    start(now);
                else
                    ctx.wake_at(issue_at);
                return;
            }
            mem.done.clear();
            resume_pc.reset();
            if (r == riscv::step_result::waiting) return;
            t_core += core_period;
            if (stepping) {
                stepping = false;
                ctx.request_pause("stepped to " + hex32(h.pc));
                paused_at = now;
                return;
            }
        }
        ctx.wake_at(t_core);
    }

    // puts the wanted request on the bus
    void start(sim::time now)
    {
        (void)now;
        auto& w = mem.want;
        xfer = {};
        xfer.active = true;
        xfer.req = w;
        transactions++;
        int line = mem.dc.line_bytes;
        switch (w.k) {
        case request::kind::fill_i:
        case request::kind::fill_d:
            xfer.addr = w.addr;
            xfer.beats = line / bus_bytes;
            xfer.size = log2_of(bus_bytes);
            xfer.buf.assign(size_t(line), 0);
            break;
        case request::kind::writeback:
            xfer.write = true;
            xfer.addr = w.addr;
            xfer.beats = line / bus_bytes;
            xfer.size = log2_of(bus_bytes);
            xfer.buf.assign(mem.dc.bytes(w.line), mem.dc.bytes(w.line) + line);
            break;
        case request::kind::read:
            xfer.addr = w.addr;
            xfer.size = log2_of(w.size);
            xfer.buf.assign(4, 0);
            break;
        case request::kind::write: {
            xfer.write = true;
            xfer.addr = w.addr;
            xfer.size = log2_of(w.size);
            xfer.buf.assign(4, 0);
            std::memcpy(xfer.buf.data(), &w.data, 4);
            break;
        }
        case request::kind::none: break;
        }
        w = {};
        out_dirty = true;
    }

    bool cacheable_txn() const { return xfer.req.k == request::kind::fill_i || xfer.req.k == request::kind::fill_d || xfer.req.k == request::kind::writeback; }

    // the handshakes at a rising edge, as the signals were before it
    void edge(const value& p, sim::time now)
    {
        if (!xfer.write) {
            if (!xfer.addr_done && high(p, l.arready)) {
                xfer.addr_done = true;
                out_dirty = true;
            }
            if (high(p, l.rvalid)) {
                if (field(p, l.rresp.off, l.rresp.w) & 2) xfer.error = true;
                if (cacheable_txn())
                    value_bytes(p, l.rdata.off, xfer.buf.data() + size_t(xfer.beat) * bus_bytes, bus_bytes);
                else
                    value_bytes(p, l.rdata.off + 8 * int(xfer.addr % uint32_t(bus_bytes)), xfer.buf.data(), 1 << xfer.size);
                if (++xfer.beat >= xfer.beats) finish(now);
            }
            return;
        }
        if (!xfer.addr_done && high(p, l.awready)) {
            xfer.addr_done = true;
            out_dirty = true;
        }
        if (xfer.beat < xfer.beats && high(p, l.wready)) {
            xfer.beat++;
            out_dirty = true;
        }
        if (high(p, l.bvalid)) {
            if (field(p, l.bresp.off, l.bresp.w) & 2) xfer.error = true;
            finish(now);
        }
    }

    // the transaction is done: the caches or the completed list get its
    // result and the hart goes on
    void finish(sim::time now)
    {
        auto& r = xfer.req;
        switch (r.k) {
        case request::kind::fill_i:
        case request::kind::fill_d: {
            auto& c = r.k == request::kind::fill_i ? mem.ic : mem.dc;
            if (xfer.error) {
                mem.done.push_back({r.k, r.addr, 0, 0, false});
            } else {
                c.install(r.line, r.addr, xfer.buf.data());
                c.misses++;
            }
            break;
        }
        case request::kind::writeback: mem.dc.lines[r.line].dirty = false; break;
        case request::kind::read: {
            uint32_t v = 0;
            std::memcpy(&v, xfer.buf.data(), 4);
            if (r.size < 4) v &= (1u << (8 * r.size)) - 1;
            mem.done.push_back({r.k, r.addr, r.size, v, !xfer.error});
            break;
        }
        case request::kind::write: mem.done.push_back({r.k, r.addr, r.size, 0, !xfer.error}); break;
        case request::kind::none: break;
        }
        // the cycles the hart waited
        if (core_period && now > issue_at) h.cycle += (now - issue_at) / core_period;
        xfer = {};
        resumed = true;
        out_dirty = true;
    }

    value address_beat(const channel& c, const axi::sig& addr, const axi::sig& len, const axi::sig& size, const axi::sig& burst, const axi::sig& cache_s,
        const axi::sig& prot) const
    {
        auto v = zeros(c.w);
        set(v, c, addr, xfer.addr);
        set(v, c, len, uint64_t(xfer.beats - 1));
        set(v, c, size, uint64_t(xfer.size));
        set(v, c, burst, 1);
        // write-back cacheable or device memory
        set(v, c, cache_s, cacheable_txn() ? 0xF : 0x0);
        bool instruction = xfer.req.k == request::kind::fill_i;
        set(v, c, prot, (xfer.req.privileged ? 1 : 0) | (instruction ? 4 : 0));
        return v;
    }

    value write_beat() const
    {
        auto v = zeros(l.w.w);
        value data = zeros(l.wdata.w);
        value strb = zeros(l.wstrb.w);
        if (cacheable_txn()) {
            data = bytes_value(xfer.buf.data() + size_t(xfer.beat) * bus_bytes, bus_bytes);
            for (int k = 0; k < bus_bytes; k++)
                strb.insert(k, sim::known(1, 1));
        } else {
            int lane = int(xfer.addr % uint32_t(bus_bytes)), n = 1 << xfer.size;
            data.insert(8 * lane, bytes_value(xfer.buf.data(), n));
            for (int k = 0; k < n; k++)
                strb.insert(lane + k, sim::known(1, 1));
        }
        set(v, l.w, l.wdata, data);
        set(v, l.w, l.wstrb, strb);
        set(v, l.w, l.wlast, xfer.beat == xfer.beats - 1);
        return v;
    }

    void update_outputs()
    {
        axi::drive d(l.total);
        bool reading = xfer.active && !xfer.write, writing = xfer.active && xfer.write;
        value aw, w, ar;
        if (writing && !xfer.addr_done) aw = address_beat(l.aw, l.awaddr, l.awlen, l.awsize, l.awburst, l.awcache, l.awprot);
        if (writing && xfer.beat < xfer.beats) w = write_beat();
        if (reading && !xfer.addr_done) ar = address_beat(l.ar, l.araddr, l.arlen, l.arsize, l.arburst, l.arcache, l.arprot);
        d.source(l.aw, aw.width() ? &aw : nullptr);
        d.source(l.w, w.width() ? &w : nullptr);
        d.source(l.ar, ar.width() ? &ar : nullptr);
        d.sink(l.b, writing);
        d.sink(l.r, reading);
        out = std::move(d.v);
        out_dirty = false;
    }

    cpu_config cfg;
    layout l;
    int bus_bytes;
    memory_system mem;
    riscv::hart h;

    sim::bit_state prev_clk = sim::bit_state::z;
    sim::time last_rise = sim::never;
    // the time of one instruction, aclk's period / ratio, 0 until aclk
    // rose twice
    sim::time core_period = 0;
    bool in_reset = false;
    // the time of the hart's next instruction, ahead of the simulation by
    // up to run_ahead
    sim::time t_core = 0;
    // when the stalled access wants the bus
    sim::time issue_at = 0;
    // a transaction finished, the hart goes on from now
    bool resumed = false;
    transfer xfer;
    uint64_t transactions = 0;

    std::set<uint32_t> breaks;
    // the breakpoint the simulation paused at, passed when it goes on
    std::optional<uint32_t> resume_pc;
    bool stepping = false;
    // a pause was asked for at that time, the hart waits until it's over
    sim::time paused_at = sim::never;

    value out;
    bool out_dirty = true;
};

std::vector<model::param_def> cpu_params()
{
    std::vector<model::param_def> r;
    for (auto& p : axi::params())
        if (p.name != "protocol") r.push_back(p);
    r.push_back(model::text_param("reset_pc", "Reset pc", "0x80000000", "where execution starts after reset"));
    r.push_back(model::int_param("hartid", "Hart id", 0, 0, 1023, "mhartid, and a0 after reset"));
    r.push_back(model::text_param("a1", "a1 at reset", "0x87000000", "e.g. the address of the device tree"));
    r.push_back(model::int_param("ratio", "Core clock ratio", 8, 1, 256, "core cycles per aclk cycle, an instruction per core cycle when it doesn't wait for the bus"));
    r.push_back(model::int_param("run_ahead", "Run ahead (ns)", 1000, 0, 1000000, "how far the core may run ahead of the simulation: interrupts are seen up to this late"));
    r.push_back(model::int_param("timebase", "Timebase (Hz)", 10000000, 1, 1000000000, "frequency of the time csr, the same as the clint's"));
    r.push_back(model::int_param("icache", "I-cache (KiB)", 16, 0, 1024, "a power of two, 0 for none"));
    r.push_back(model::int_param("dcache", "D-cache (KiB)", 16, 0, 1024, "a power of two, 0 for none: every access goes to the bus"));
    r.push_back(model::int_param("ways", "Ways", 4, 1, 16, "associativity of both caches, a power of two"));
    r.push_back(model::int_param("line", "Line (bytes)", 64, 16, 1024, "a power of two, at least a bus beat. a miss is a burst of a line"));
    r.push_back(model::text_param("cacheable", "Cacheable", "0x80000000/2G",
        "base/size ranges the caches hold, e.g. the ram. other addresses are single uncached accesses in program order"));
    return r;
}

class cpu : public primitive {
public:
    std::string kind() const override { return "rv32_cpu"; }
    std::string name() const override { return "RV32IMA CPU"; }
    std::string category() const override { return "RISC-V"; }
    std::string description() const override
    {
        return "a risc-v hart (rv32ima, zicsr, zifencei, m/s/u modes, sv32) with instruction and data caches on an AXI4 master port";
    }

    std::vector<model::param_def> params() const override { return cpu_params(); }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto w = axi::usable(protocol::axi4, axi::widths_of(v));
        int total = axi::make_layout(protocol::axi4, w).total;
        builder b;
        b.box("RV32IMA",
            {axi::clock_pin(), axi::reset_pin(), {"msip", direction::input, 1, "machine software interrupt, from the clint"},
                {"mtip", direction::input, 1, "machine timer interrupt, from the clint"}, {"meip", direction::input, 1, "machine external interrupt, from the plic"},
                {"seip", direction::input, 1, "supervisor external interrupt, from the plic"}},
            {{"m", direction::inout, total, "memory and devices, e.g. through a crossbar", {}, true}}, {}, 8);
        return axi::finish_ports(b, protocol::axi4, w, {{"m", false}});
    }

    std::string check(const model::param_values& v) const override
    {
        auto w = axi::widths_of(v);
        if (auto e = axi::check_widths(protocol::axi4, w); !e.empty()) return e;
        if (w.data < 32) return "the cpu needs at least 32 data bits";
        if (!axi::parse_number(model::get_string(v, "reset_pc"))) return "the reset pc isn't a number";
        if (!axi::parse_number(model::get_string(v, "a1"))) return "a1 isn't a number";
        int64_t ic = model::get_int(v, "icache"), dc = model::get_int(v, "dcache"), ways = model::get_int(v, "ways"), line = model::get_int(v, "line");
        if ((ic && !power_of_two(ic)) || (dc && !power_of_two(dc))) return "the cache sizes must be powers of two";
        if (!power_of_two(ways)) return "the ways must be a power of two";
        if (!power_of_two(line)) return "the line size must be a power of two";
        if (line < w.data / 8) return "a line must be at least a bus beat";
        if (line / (w.data / 8) > 256) return "a line must be at most 256 bus beats";
        if ((ic && ic * 1024 < ways * line) || (dc && dc * 1024 < ways * line)) return "a cache must hold a line per way";
        std::string error;
        if (!axi::parse_ranges(model::get_string(v, "cacheable"), 32, error)) return error;
        return {};
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        if (!check(v).empty()) return nullptr;
        cpu_config c;
        c.l = axi::make_layout(protocol::axi4, axi::usable(protocol::axi4, axi::widths_of(v)));
        c.reset_pc = uint32_t(*axi::parse_number(model::get_string(v, "reset_pc")));
        c.hartid = uint32_t(model::get_int(v, "hartid"));
        c.a1 = uint32_t(*axi::parse_number(model::get_string(v, "a1")));
        c.ratio = int(model::get_int(v, "ratio"));
        c.run_ahead = sim::time(model::get_int(v, "run_ahead")) * sim::ns;
        c.timebase = uint64_t(model::get_int(v, "timebase"));
        c.icache = int(model::get_int(v, "icache")) * 1024;
        c.dcache = int(model::get_int(v, "dcache")) * 1024;
        c.ways = int(model::get_int(v, "ways"));
        c.line = int(model::get_int(v, "line"));
        std::string error;
        c.cacheable = *axi::parse_ranges(model::get_string(v, "cacheable"), 32, error);
        return std::make_unique<cpu_component>(c);
    }
};

}

void add_riscv(model::primitive_registry& r)
{
    r.add(std::make_unique<cpu>());
}

}
