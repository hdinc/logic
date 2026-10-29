// the risc-v parts: an rv32ima cpu with caches on an axi4 master port. the
// hart is in src/riscv, after riscv-simulator by hdinc and uwrk9amtdn.

#include "axi_parts.h"
#include "builder.h"
#include "builtin.h"
#include "../riscv/hart.h"

#include <cstdio>
#include <cstring>
#include <deque>
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

// an axi slave of registers: a beat at a time, bursts beat by beat. the
// device sees accesses of 1 to 8 bytes at an offset in its window: the
// address modulo the window's size, so the base must be aligned to it.
class axi_regs : public axi::clocked {
public:
    axi_regs(const layout& l, uint64_t window, int s, int clk, int rst)
        : clocked(clk, rst)
        , l(l)
        , window(window)
        , s_port(s)
    {
    }

protected:
    // false answers SLVERR
    virtual bool reg_read(uint64_t offset, int bytes, uint64_t& value) = 0;
    virtual bool reg_write(uint64_t offset, int bytes, uint64_t value) = 0;
    // the size of an AXI4-Lite read, which has no size of its own
    virtual int reg_bytes() const { return 4; }
    virtual void device_clear() = 0;
    // the device's other outputs, e.g. interrupts
    virtual void device_outputs(std::span<value> ports) = 0;

    void clear() override
    {
        aw = {}, w = {}, ar = {}, b = {}, r = {};
        wr = {}, rd = {};
        device_clear();
    }

    void edge(std::span<const value> ports) override
    {
        auto& p = ports[s_port];
        bool aw_ready = aw.ready(), w_ready = w.ready(), ar_ready = ar.ready();
        if (b.head() && high(p, l.bready)) b.pop();
        if (r.head() && high(p, l.rready)) r.pop();
        int bus = l.wdata.w / 8;

        if (!wr.active && aw.head()) wr = start(aw.pop(), l.aw, l.awid, l.awaddr, l.awlen, l.awsize, l.awburst);
        if (wr.active && w.head() && b.ready()) {
            auto beat = w.pop();
            auto a = axi::beat_address(wr.addr, wr.size, wr.kind, wr.len, wr.beat) & ~uint64_t(bus - 1);
            // the bytes with strobes, from the first to the last
            int first = -1, last = -1;
            for (int k = 0; k < bus; k++)
                if (high(beat, l.wstrb.off - l.w.off + k)) {
                    if (first < 0) first = k;
                    last = k;
                }
            if (first >= 0 && last - first < 8) {
                uint64_t v = field(beat, l.wdata.off - l.w.off + 8 * first, 8 * (last - first + 1));
                if (!reg_write((a + uint64_t(first)) % window, last - first + 1, v)) wr.error = true;
            }
            bool done = l.proto == protocol::lite || field(beat, l.w, l.wlast) || wr.beat >= wr.len;
            wr.beat++;
            if (done) {
                auto resp = zeros(l.b.w);
                set(resp, l.b, l.bid, wr.id);
                set(resp, l.b, l.bresp, wr.error ? 2 : axi::okay);
                b.q.push_back(std::move(resp));
                wr.active = false;
            }
        }

        if (!rd.active && ar.head()) rd = start(ar.pop(), l.ar, l.arid, l.araddr, l.arlen, l.arsize, l.arburst);
        if (rd.active && r.ready()) {
            uint64_t a = l.proto == protocol::lite ? rd.addr : axi::beat_address(rd.addr, rd.size, rd.kind, rd.len, rd.beat);
            int bytes = l.proto == protocol::lite ? reg_bytes() : std::min(1 << rd.size, 8);
            uint64_t v = 0;
            bool ok = reg_read(a % window, bytes, v);
            auto beat = zeros(l.r.w);
            value data = zeros(l.rdata.w);
            int lane = int(a % uint64_t(bus));
            if (lane + bytes <= bus) data.insert(8 * lane, sim::known(v & sim::mask(8 * bytes), 8 * bytes));
            set(beat, l.r, l.rid, rd.id);
            set(beat, l.r, l.rdata, data);
            set(beat, l.r, l.rresp, ok ? axi::okay : 2);
            set(beat, l.r, l.rlast, rd.beat >= rd.len);
            r.q.push_back(std::move(beat));
            if (rd.beat++ >= rd.len) rd.active = false;
        }

        if (aw_ready && high(p, l.awvalid)) aw.q.push_back(p.slice(l.aw.off, l.aw.w));
        if (w_ready && high(p, l.wvalid)) w.q.push_back(p.slice(l.w.off, l.w.w));
        if (ar_ready && high(p, l.arvalid)) ar.q.push_back(p.slice(l.ar.off, l.ar.w));
    }

    void outputs(std::span<value> ports) override
    {
        axi::drive d(l.total);
        d.sink(l.aw, aw.ready());
        d.sink(l.w, w.ready());
        d.sink(l.ar, ar.ready());
        d.source(l.b, b.head());
        d.source(l.r, r.head());
        ports[s_port] = std::move(d.v);
        device_outputs(ports);
    }

    layout l;

private:
    struct burst {
        bool active = false;
        uint64_t addr = 0;
        int size = 0, kind = 1, len = 0, beat = 0;
        bool error = false;
        value id;
    };

    burst start(const value& beat, const channel& c, const axi::sig& id, const axi::sig& addr, const axi::sig& len, const axi::sig& size, const axi::sig& kind) const
    {
        burst t;
        t.active = true;
        t.addr = field(beat, c, addr);
        t.id = id.w ? beat.slice(id.off - c.off, id.w) : value();
        if (l.proto == protocol::lite) {
            t.size = log2_of(l.wdata.w / 8);
            return t;
        }
        t.len = (int)field(beat, c, len);
        t.size = (int)field(beat, c, size);
        t.kind = (int)field(beat, c, kind);
        return t;
    }

    uint64_t window;
    int s_port;
    axi::fifo aw, w, ar, b, r;
    burst wr, rd;
};

value level(bool on) { return sim::known(on ? 1 : 0, 1); }

// the core local interruptor: mtime from the simulation time, a mtimecmp and
// msip per hart. ports: s, aclk, aresetn, then msip and mtip per hart.
class clint_component : public axi_regs {
public:
    clint_component(const layout& l, int harts, uint64_t timebase)
        : axi_regs(l, 0x10000, 0, 1, 2)
        , harts(harts)
        , timebase(timebase)
    {
        reset();
    }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        now = ctx.now();
        axi_regs::eval(ports, ctx);
        // the next time a timer goes off
        sim::time next = sim::never;
        for (int k = 0; k < harts; k++)
            if (mtimecmp[k] > mtime()) next = std::min(next, time_of(mtimecmp[k]));
        ctx.wake_at(next);
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        out.push_back({"mtime", sim::known(mtime(), 64), {}});
        for (int k = 0; k < harts; k++) {
            auto n = harts > 1 ? std::to_string(k) : std::string();
            out.push_back({"mtimecmp" + n, sim::known(mtimecmp[k], 64), {}});
            out.push_back({"msip" + n, sim::known(msip[k], 1), {}});
        }
    }

private:
    uint64_t mtime() const
    {
        __extension__ typedef unsigned __int128 wide;
        return uint64_t(wide(now) * timebase / sim::seconds) + offset;
    }
    // the simulation time at which mtime reaches v
    sim::time time_of(uint64_t v) const
    {
        __extension__ typedef unsigned __int128 wide;
        wide ticks = v - offset;
        wide t = (ticks * sim::seconds + timebase - 1) / timebase;
        return t > wide(sim::never - 1) ? sim::never : sim::time(t);
    }

    void device_clear() override
    {
        msip.assign(size_t(harts), 0);
        mtimecmp.assign(size_t(harts), ~uint64_t(0));
        offset = 0;
    }

    // a 64 bit register, read or written in halves or whole
    static bool part(uint64_t offset_in, int bytes, int& shift)
    {
        if (bytes == 8 && offset_in % 8 == 0) shift = 0;
        else if (bytes == 4 && offset_in % 4 == 0) shift = int(offset_in % 8) * 8;
        else return false;
        return true;
    }

    bool reg_read(uint64_t a, int bytes, uint64_t& v) override
    {
        int shift = 0;
        if (a < 0x4000) {
            if (bytes != 4 || a % 4) return false;
            v = a / 4 < uint64_t(harts) ? msip[a / 4] : 0;
            return true;
        }
        if (a >= 0xBFF8) {
            if (!part(a, bytes, shift)) return false;
            v = mtime() >> shift;
            return true;
        }
        if (!part(a, bytes, shift)) return false;
        uint64_t k = (a - 0x4000) / 8;
        v = k < uint64_t(harts) ? mtimecmp[k] >> shift : 0;
        return true;
    }

    static uint64_t merge(uint64_t old, uint64_t v, int bytes, int shift)
    {
        if (bytes == 8) return v;
        uint64_t m = 0xFFFFFFFFull << shift;
        return (old & ~m) | ((v << shift) & m);
    }

    bool reg_write(uint64_t a, int bytes, uint64_t v) override
    {
        int shift = 0;
        if (a < 0x4000) {
            if (bytes != 4 || a % 4) return false;
            if (a / 4 < uint64_t(harts)) msip[a / 4] = v & 1;
            return true;
        }
        if (a >= 0xBFF8) {
            if (!part(a, bytes, shift)) return false;
            offset += merge(mtime(), v, bytes, shift) - mtime();
            return true;
        }
        if (!part(a, bytes, shift)) return false;
        uint64_t k = (a - 0x4000) / 8;
        if (k < uint64_t(harts)) mtimecmp[k] = merge(mtimecmp[k], v, bytes, shift);
        return true;
    }

    void device_outputs(std::span<value> ports) override
    {
        uint64_t t = mtime();
        for (int k = 0; k < harts; k++) {
            ports[3 + 2 * k] = level(msip[k]);
            ports[4 + 2 * k] = level(t >= mtimecmp[k]);
        }
    }

    int harts;
    uint64_t timebase;
    sim::time now = 0;
    // mtime minus the time base's count, changed by writes to mtime
    uint64_t offset = 0;
    std::vector<uint32_t> msip;
    std::vector<uint64_t> mtimecmp;
};

// the platform level interrupt controller with a machine and a supervisor
// context: level triggered sources 1..n, priorities 0..7, claim/complete.
// ports: s, aclk, aresetn, irq1..irqn, meip, seip.
class plic_component : public axi_regs {
public:
    plic_component(const layout& l, int sources)
        : axi_regs(l, 0x400000, 0, 1, 2)
        , sources(sources)
    {
        reset();
    }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        uint32_t lines = 0;
        for (int k = 1; k <= sources; k++)
            if (ports[2 + k].get(0) == sim::bit_state::one) lines |= 1u << k;
        signal = lines;
        axi_regs::eval(ports, ctx);
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        out.push_back({"pending", sim::known(pending(), 32), {}});
        out.push_back({"claimed", sim::known(claimed, 32), {}});
        for (int c = 0; c < 2; c++) {
            auto n = std::string(c ? "s " : "m ");
            out.push_back({n + "enable", sim::known(enable[c], 32), {}});
            out.push_back({n + "threshold", sim::known(threshold[c], 3), {}});
        }
    }

private:
    static constexpr int contexts = 2;

    uint32_t pending() const { return signal & ~claimed & ~1u; }

    // the pending and enabled source with the highest priority above 0, the
    // lowest id first, 0 if none
    uint32_t best(int c) const
    {
        uint32_t id = 0, pri = 0;
        for (int k = 1; k <= sources; k++)
            if ((pending() & enable[c] & (1u << k)) && priority[k] > pri) {
                pri = priority[k];
                id = uint32_t(k);
            }
        return id;
    }

    bool asserted(int c) const
    {
        auto id = best(c);
        return id && priority[id] > threshold[c];
    }

    void device_clear() override
    {
        priority.assign(32, 0);
        enable[0] = enable[1] = 0;
        threshold[0] = threshold[1] = 0;
        claimed = 0;
    }

    bool reg_read(uint64_t a, int bytes, uint64_t& v) override
    {
        if (bytes != 4 || a % 4) return false;
        v = 0;
        if (a < 0x1000) {
            if (a / 4 < 32) v = priority[a / 4];
        } else if (a == 0x1000) {
            v = pending();
        } else if (a >= 0x2000 && a < 0x200000) {
            uint64_t c = (a - 0x2000) / 0x80;
            if (c < contexts && (a - 0x2000) % 0x80 == 0) v = enable[c];
        } else if (a >= 0x200000) {
            uint64_t c = (a - 0x200000) / 0x1000, r = (a - 0x200000) % 0x1000;
            if (c < contexts && r == 0) v = threshold[c];
            if (c < contexts && r == 4) {
                // claim: the source stays out until it's completed
                auto id = best(int(c));
                if (id) claimed |= 1u << id;
                v = id;
            }
        }
        return true;
    }

    bool reg_write(uint64_t a, int bytes, uint64_t v) override
    {
        if (bytes != 4 || a % 4) return false;
        uint32_t m = sources >= 31 ? ~1u : ((2u << sources) - 1) & ~1u;
        if (a < 0x1000) {
            if (a / 4 >= 1 && a / 4 <= uint64_t(sources)) priority[a / 4] = uint32_t(v & 7);
        } else if (a >= 0x2000 && a < 0x200000) {
            uint64_t c = (a - 0x2000) / 0x80;
            if (c < contexts && (a - 0x2000) % 0x80 == 0) enable[c] = uint32_t(v) & m;
        } else if (a >= 0x200000) {
            uint64_t c = (a - 0x200000) / 0x1000, r = (a - 0x200000) % 0x1000;
            if (c < contexts && r == 0) threshold[c] = uint32_t(v & 7);
            // complete: ignored for sources the context doesn't enable
            if (c < contexts && r == 4 && v >= 1 && v <= uint64_t(sources) && (enable[c] & (1u << v))) claimed &= ~(1u << v);
        }
        return true;
    }

    void device_outputs(std::span<value> ports) override
    {
        ports[3 + sources] = level(asserted(0));
        ports[4 + sources] = level(asserted(1));
    }

    int sources;
    uint32_t signal = 0;
    std::vector<uint32_t> priority;
    uint32_t enable[contexts] = {};
    uint32_t threshold[contexts] = {};
    // sources that are claimed and not completed yet
    uint32_t claimed = 0;
};

// an ns16550a: 8 byte registers, a 16 byte receive fifo. sending takes no
// time, the bytes go to a terminal window; typed bytes are received at the
// baud rate set by the divisor. ports: s, aclk, aresetn, irq.
class uart_component : public axi_regs {
public:
    uart_component(const layout& l, uint64_t clock_hz)
        : axi_regs(l, 8, 0, 1, 2)
        , clock_hz(clock_hz)
    {
        reset();
    }

    void reset() override
    {
        axi_regs::reset();
        out->reset();
        typed.clear();
    }

    std::shared_ptr<sim::byte_stream> terminal() const override { return out; }

    void eval(std::span<value> ports, sim::context& ctx) override
    {
        now = ctx.now();
        // typed bytes enter the fifo a character time apart
        while (!typed.empty() && now >= next_rx) {
            receive(typed.front());
            typed.pop_front();
            next_rx = now + char_time();
            last_rx_activity = now;
        }
        // the character timeout: data below the trigger level, 4 characters
        // without anything happening
        if (!rx.empty() && !timeout && last_rx_activity != sim::never && now >= last_rx_activity + 4 * char_time()) timeout = true;
        axi_regs::eval(ports, ctx);
        sim::time wake = typed.empty() ? sim::never : std::max(next_rx, now + 1);
        if (!rx.empty() && !timeout && last_rx_activity != sim::never) wake = std::min(wake, last_rx_activity + 4 * char_time());
        ctx.wake_at(wake);
    }

    bool interact(const std::string& action, int64_t v) override
    {
        if (action != "key") return false;
        typed.push_back(uint8_t(v));
        return true;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        out.push_back({"ier", sim::known(ier, 8), {}});
        out.push_back({"lcr", sim::known(lcr, 8), {}});
        out.push_back({"lsr", sim::known(lsr(), 8), {}});
        out.push_back({"divisor", sim::known(uint64_t(dlm) << 8 | dll, 16), {}});
        out.push_back({"rx fifo", sim::known(rx.size(), 5), {}});
        out.push_back({"typed", sim::known(typed.size(), 32), {}});
    }

    int reg_bytes() const override { return 1; }

private:
    static constexpr uint8_t ier_rx = 1, ier_thre = 2, ier_ls = 4;
    static constexpr uint8_t lcr_dlab = 0x80;
    static constexpr uint8_t mcr_loop = 0x10;
    static constexpr size_t fifo_size = 16;

    // 10 bits at the divisor's baud rate, 115200 baud while it's 0
    sim::time char_time() const
    {
        uint64_t div = uint64_t(dlm) << 8 | dll;
        uint64_t baud = div ? std::max<uint64_t>(1, clock_hz / (16 * div)) : 115200;
        return sim::time(10 * sim::seconds / baud);
    }

    uint8_t lsr() const { return uint8_t((rx.empty() ? 0 : 1) | (overrun ? 2 : 0) | 0x60); }

    int trigger() const
    {
        static const int levels[4] = {1, 4, 8, 14};
        return fifo_on ? levels[(fcr >> 6) & 3] : 1;
    }

    // the interrupt being reported: 0xC1 bits of iir, 1 for none
    uint8_t iir_id() const
    {
        if ((ier & ier_ls) && overrun) return 0x6;
        if ((ier & ier_rx) && (int(rx.size()) >= trigger() || (timeout && !rx.empty()))) return timeout && int(rx.size()) < trigger() ? 0xC : 0x4;
        if ((ier & ier_thre) && thre) return 0x2;
        return 0x1;
    }

    bool irq() const { return iir_id() != 0x1; }

    void receive(uint8_t c)
    {
        size_t room = fifo_on ? fifo_size : 1;
        if (rx.size() >= room) {
            overrun = true;
            if (!fifo_on) rx.back() = c;
            return;
        }
        rx.push_back(c);
        timeout = false;
    }

    void send(uint8_t c)
    {
        if (mcr & mcr_loop) {
            receive(c);
            last_rx_activity = now;
            return;
        }
        out->append(char(c));
    }

    void device_clear() override
    {
        rx.clear();
        ier = lcr = mcr = scr = fcr = dll = dlm = 0;
        fifo_on = overrun = timeout = false;
        // nothing to send, but no interrupt until ier asks for one
        thre = false;
        next_rx = 0;
        last_rx_activity = sim::never;
    }

    bool reg_read(uint64_t a, int bytes, uint64_t& v) override
    {
        if (bytes != 1) return false;
        switch (a) {
        case 0:
            if (lcr & lcr_dlab) {
                v = dll;
            } else {
                v = rx.empty() ? 0 : rx.front();
                if (!rx.empty()) rx.pop_front();
                timeout = false;
                last_rx_activity = now;
            }
            break;
        case 1: v = (lcr & lcr_dlab) ? dlm : ier; break;
        case 2: {
            auto id = iir_id();
            v = id | (fifo_on ? 0xC0 : 0);
            // reading iir clears the thre interrupt it reports
            if (id == 0x2) thre = false;
            break;
        }
        case 3: v = lcr; break;
        case 4: v = mcr; break;
        case 5:
            v = lsr();
            overrun = false;
            break;
        // dcd, dsr and cts on, or in loop mode what mcr drives
        case 6: v = (mcr & mcr_loop) ? ((mcr & 0xC) << 4 | (mcr & 3) << 4) : 0xB0; break;
        case 7: v = scr; break;
        default: return false;
        }
        return true;
    }

    bool reg_write(uint64_t a, int bytes, uint64_t value64) override
    {
        if (bytes != 1) return false;
        uint8_t v = uint8_t(value64);
        switch (a) {
        case 0:
            if (lcr & lcr_dlab) {
                dll = v;
            } else {
                send(v);
                // sent at once: thr is empty again
                thre = true;
            }
            break;
        case 1:
            if (lcr & lcr_dlab) {
                dlm = v;
            } else {
                // enabling the thre interrupt while thr is empty raises it
                if ((v & ier_thre) && !(ier & ier_thre)) thre = true;
                ier = v & 0x0F;
            }
            break;
        case 2:
            if (bool(v & 1) != fifo_on) rx.clear();
            fifo_on = v & 1;
            fcr = v & 0xC1;
            if (v & 2) rx.clear();
            break;
        case 3: lcr = v; break;
        case 4: mcr = v & 0x1F; break;
        case 7: scr = v; break;
        default: break;
        }
        return true;
    }

    void device_outputs(std::span<value> ports) override { ports[3] = level(irq()); }

    uint64_t clock_hz;
    sim::time now = 0;
    std::deque<uint8_t> rx;
    // typed in the simulation's terminal, not received yet
    std::deque<uint8_t> typed;
    sim::time next_rx = 0;
    sim::time last_rx_activity = sim::never;
    uint8_t ier = 0, lcr = 0, mcr = 0, scr = 0, fcr = 0, dll = 0, dlm = 0;
    bool fifo_on = false, overrun = false, timeout = false, thre = false;
    // what's sent, for the terminal window
    std::shared_ptr<sim::byte_stream> out = std::make_shared<sim::byte_stream>();
};

// the slave parts' axi parameters, AXI4 or AXI4-Lite
model::cell_interface slave_part(const model::param_values& v, const std::string& title, std::vector<builder::box_pin> left, std::vector<builder::box_pin> right)
{
    auto proto = axi::protocol_of(v);
    auto w = axi::usable(proto, axi::widths_of(v));
    int total = axi::make_layout(proto, w).total;
    left.insert(left.begin(), {{"s", direction::inout, total, "the cpu connects here, e.g. through a crossbar", {}, true}, axi::clock_pin(), axi::reset_pin()});
    builder b;
    b.box(title, left, right, {}, 6);
    return axi::finish_ports(b, proto, w, {{"s", true}});
}

layout slave_layout(const model::param_values& v)
{
    auto proto = axi::protocol_of(v);
    return axi::make_layout(proto, axi::usable(proto, axi::widths_of(v)));
}

class clint : public primitive {
public:
    std::string kind() const override { return "riscv_clint"; }
    std::string name() const override { return "CLINT"; }
    std::string category() const override { return "RISC-V"; }
    std::string description() const override { return "core local interruptor: mtime, mtimecmp and msip per hart (riscv,clint0), a 64 KiB window"; }

    std::vector<model::param_def> params() const override
    {
        auto r = axi::params();
        r.push_back(model::int_param("harts", "Harts", 1, 1, 8));
        r.push_back(model::int_param("timebase", "Timebase (Hz)", 10000000, 1, 1000000000, "mtime counts at this rate, the cpus' timebase"));
        return r;
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int harts = (int)model::get_int(v, "harts");
        std::vector<builder::box_pin> right;
        for (int k = 0; k < harts; k++) {
            auto n = harts > 1 ? std::to_string(k) : std::string();
            right.push_back({"msip" + n, direction::output, 1, "software interrupt of hart " + std::to_string(k)});
            right.push_back({"mtip" + n, direction::output, 1, "timer interrupt of hart " + std::to_string(k) + ": mtime >= mtimecmp"});
        }
        return slave_part(v, "CLINT", {}, right);
    }

    std::string check(const model::param_values& v) const override { return axi::check_widths(axi::protocol_of(v), axi::widths_of(v)); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<clint_component>(slave_layout(v), (int)model::get_int(v, "harts"), uint64_t(model::get_int(v, "timebase")));
    }
};

class plic : public primitive {
public:
    std::string kind() const override { return "riscv_plic"; }
    std::string name() const override { return "PLIC"; }
    std::string category() const override { return "RISC-V"; }
    std::string description() const override
    {
        return "platform level interrupt controller (riscv,plic0): level triggered sources, a machine and a supervisor context, a 4 MiB window";
    }

    std::vector<model::param_def> params() const override
    {
        auto r = axi::params();
        r.push_back(model::int_param("sources", "Sources", 8, 1, 31, "interrupt inputs irq1.., riscv,ndev"));
        return r;
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        int n = (int)model::get_int(v, "sources");
        std::vector<builder::box_pin> left;
        for (int k = 1; k <= n; k++)
            left.push_back({"irq" + std::to_string(k), direction::input, 1, "interrupt source " + std::to_string(k) + ", high while pending"});
        return slave_part(v, "PLIC", left,
            {{"meip", direction::output, 1, "machine external interrupt, to the cpu"}, {"seip", direction::output, 1, "supervisor external interrupt, to the cpu"}});
    }

    std::string check(const model::param_values& v) const override { return axi::check_widths(axi::protocol_of(v), axi::widths_of(v)); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<plic_component>(slave_layout(v), (int)model::get_int(v, "sources"));
    }
};

class uart : public primitive {
public:
    std::string kind() const override { return "uart16550"; }
    std::string name() const override { return "UART 16550"; }
    std::string category() const override { return "RISC-V"; }
    std::string description() const override { return "an ns16550a with a terminal in the simulation: 8 byte registers, a 16 byte receive fifo, an 8 byte window"; }

    std::vector<model::param_def> params() const override
    {
        auto r = axi::params();
        r.push_back(model::int_param("clock", "Clock (Hz)", 10000000, 1, 1000000000, "the baud clock, the device tree's clock-frequency"));
        return r;
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        return slave_part(v, "UART", {}, {{"irq", direction::output, 1, "interrupt, to the plic"}});
    }

    std::string check(const model::param_values& v) const override { return axi::check_widths(axi::protocol_of(v), axi::widths_of(v)); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        return std::make_unique<uart_component>(slave_layout(v), uint64_t(model::get_int(v, "clock")));
    }
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
    r.add(std::make_unique<clint>());
    r.add(std::make_unique<plic>());
    r.add(std::make_unique<uart>());
}

}
