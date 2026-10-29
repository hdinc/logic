#include "axi.h"
#include "axi_parts.h"
#include "builder.h"
#include "builtin.h"
#include "../io/firmware.h"
#include "../model/expr.h"

#include <algorithm>
#include <cctype>
#include <deque>

namespace logic::builtin::axi {

using model::direction;
using model::grid;
using sim::value;

const char* bundle_name(protocol p)
{
    return p == protocol::lite ? "axi4_lite" : "axi4";
}

model::bundle_type bundle(protocol p)
{
    model::bundle_type b;
    b.name = bundle_name(p);
    bool lite = p == protocol::lite;
    b.description = lite ? "AXI4-Lite, the master's view" : "AXI4, the master's view";
    if (!lite) b.params.push_back(model::int_param("ID_W", "ID_W", 4, 0, 32, "bits of the ids"));
    b.params.push_back(model::int_param("ADDR_W", "ADDR_W", 32, 12, 64, "address bits"));
    b.params.push_back(model::int_param("DATA_W", "DATA_W", 32, lite ? 32 : 8, lite ? 64 : 1024, "data bits, a power of two"));
    if (!lite) b.params.push_back(model::int_param("USER_W", "USER_W", 0, 0, 64, "bits of the user signals, 0 for none"));
    b.params.push_back(model::int_param("SIDE", "SIDE", 1, 0, 1, lite ? "1 for the prot signals" : "1 for the lock, cache, prot, qos and region signals"));

    auto add = [&](const char* name, direction dir, const char* width) {
        if (!width) return;
        model::bundle_field f;
        f.name = name;
        f.dir = dir;
        f.width = width;
        b.fields.push_back(std::move(f));
    };
    constexpr auto O = direction::output;
    constexpr auto I = direction::input;
#define LOGIC_AXI_ADD(name, dir, axi4, lite_w) add(#name, dir, lite ? lite_w : axi4);
    LOGIC_AXI_FIELDS(LOGIC_AXI_ADD)
#undef LOGIC_AXI_ADD
    return b;
}

std::vector<model::param_def> params()
{
    return {
        model::choice_param("protocol", "Protocol", {"AXI4", "AXI4-Lite"}, "AXI4"),
        model::int_param("ID_W", "ID bits", 4, 0, 32, "AXI4 only"),
        model::int_param("ADDR_W", "Address bits", 32, 12, 64),
        model::int_param("DATA_W", "Data bits", 32, 8, 1024, "a power of two, AXI4-Lite: 32 or 64"),
        model::int_param("USER_W", "User bits", 0, 0, 64, "AXI4 only, 0 for no user signals"),
        model::int_param("SIDE", "Sideband", 1, 0, 1, "1 for the lock, cache, prot, qos and region signals (AXI4-Lite: prot)"),
    };
}

protocol protocol_of(const model::param_values& v)
{
    return model::get_string(v, "protocol") == "AXI4-Lite" ? protocol::lite : protocol::axi4;
}

widths widths_of(const model::param_values& v)
{
    widths w;
    w.id = (int)model::get_int(v, "ID_W");
    w.addr = (int)model::get_int(v, "ADDR_W");
    w.data = (int)model::get_int(v, "DATA_W");
    w.user = (int)model::get_int(v, "USER_W");
    w.side = model::get_int(v, "SIDE") != 0;
    return w;
}

namespace {

model::param_values scope_of(const widths& w)
{
    return {{"ID_W", int64_t(w.id)}, {"ADDR_W", int64_t(w.addr)}, {"DATA_W", int64_t(w.data)}, {"USER_W", int64_t(w.user)}, {"SIDE", int64_t(w.side)}};
}

}

model::param_values bundle_args(protocol p, const widths& w)
{
    auto scope = scope_of(w);
    model::param_values r;
    for (auto& d : bundle(p).params)
        model::set_param(r, d, scope.at(d.name));
    return r;
}

layout make_layout(protocol p, const widths& w)
{
    layout l;
    l.proto = p;
    auto scope = scope_of(w);
    bool lite = p == protocol::lite;
    int at = 0;
    auto place = [&](sig& s, const char* width) {
        s = {at, 0};
        if (!width) return;
        std::string error;
        s.w = (int)model::eval_expr(width, scope, error).value_or(0);
        at += s.w;
    };
#define LOGIC_AXI_PLACE(name, dir, axi4, lite_w) place(l.name, lite ? lite_w : axi4);
    LOGIC_AXI_FIELDS(LOGIC_AXI_PLACE)
#undef LOGIC_AXI_PLACE
    l.total = at;
    // the payload runs from the end of the previous channel to valid
    auto chan = [](channel& c, int from, const sig& valid, const sig& ready) {
        c.off = from;
        c.w = valid.off - from;
        c.valid = valid.off;
        c.ready = ready.off;
    };
    chan(l.aw, 0, l.awvalid, l.awready);
    chan(l.w, l.awready.off + 1, l.wvalid, l.wready);
    chan(l.b, l.wready.off + 1, l.bvalid, l.bready);
    chan(l.ar, l.bready.off + 1, l.arvalid, l.arready);
    chan(l.r, l.arready.off + 1, l.rvalid, l.rready);
    return l;
}

std::optional<uint64_t> parse_number(std::string s)
{
    std::erase(s, '_');
    if (s.empty()) return std::nullopt;
    uint64_t scale = 1;
    switch (std::toupper((unsigned char)s.back())) {
    case 'K': scale = uint64_t(1) << 10; break;
    case 'M': scale = uint64_t(1) << 20; break;
    case 'G': scale = uint64_t(1) << 30; break;
    case 'T': scale = uint64_t(1) << 40; break;
    }
    if (scale != 1) s.pop_back();
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s.substr(2);
    }
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char ch : s) {
        int d = std::isdigit((unsigned char)ch) ? ch - '0' : std::isxdigit((unsigned char)ch) && base == 16 ? std::tolower((unsigned char)ch) - 'a' + 10 : -1;
        if (d < 0) return std::nullopt;
        if (__builtin_mul_overflow(v, uint64_t(base), &v) || __builtin_add_overflow(v, uint64_t(d), &v)) return std::nullopt;
    }
    if (__builtin_mul_overflow(v, scale, &v)) return std::nullopt;
    return v;
}

namespace {

std::string hex(uint64_t v)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

}

std::optional<std::vector<range>> parse_map(const std::string& text, int slaves, int addr_bits, std::string& error)
{
    std::vector<range> r;
    if (model::split_fields(text).empty()) {
        // equal power of two slices
        int bits = 0;
        while ((1 << bits) < slaves)
            bits++;
        int slice = addr_bits - bits;
        // all of a 64 bit space but its last byte
        uint64_t size = slice >= 64 ? ~uint64_t(0) : uint64_t(1) << slice;
        for (int k = 0; k < slaves; k++)
            r.push_back({slice >= 64 ? 0 : uint64_t(k) << slice, size});
        return r;
    }
    // a wrong count before overlapping ranges
    auto parsed = parse_ranges(text, addr_bits, error, "m", false);
    if (!parsed) return std::nullopt;
    if ((int)parsed->size() != slaves) {
        error = "the address map has " + std::to_string(parsed->size()) + (parsed->size() == 1 ? " range" : " ranges") + " for " + std::to_string(slaves) +
            (slaves == 1 ? " slave" : " slaves");
        return std::nullopt;
    }
    parsed = parse_ranges(text, addr_bits, error, "m");
    if (!parsed) return std::nullopt;
    return parsed;
}

std::optional<std::vector<range>> parse_ranges(const std::string& text, int addr_bits, std::string& error, const char* name, bool overlaps)
{
    uint64_t last = addr_bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << addr_bits) - 1;
    std::vector<range> r;
    for (auto& part : model::split_fields(text)) {
        auto slash = part.find('/');
        auto base = slash == std::string::npos ? std::nullopt : parse_number(part.substr(0, slash));
        auto size = slash == std::string::npos ? std::nullopt : parse_number(part.substr(slash + 1));
        // spaces around the slash
        if (slash != std::string::npos && (!base || !size)) {
            auto trim = [](std::string s) {
                std::erase_if(s, [](char ch) { return std::isspace((unsigned char)ch); });
                return s;
            };
            base = parse_number(trim(part.substr(0, slash)));
            size = parse_number(trim(part.substr(slash + 1)));
        }
        if (!base || !size || *size == 0) {
            error = "'" + part + "' is not base/size, e.g. 0x1000/4K";
            return std::nullopt;
        }
        if (*base > last || *size - 1 > last - *base) {
            error = "'" + part + "' doesn't fit in " + std::to_string(addr_bits) + " address bits";
            return std::nullopt;
        }
        r.push_back({*base, *size});
    }
    for (size_t a = 0; overlaps && a < r.size(); a++)
        for (size_t b = a + 1; b < r.size(); b++)
            if (r[a].base <= r[b].base + (r[b].size - 1) && r[b].base <= r[a].base + (r[a].size - 1)) {
                if (*name)
                    error = "the ranges of " + std::string(name) + std::to_string(a) + " (" + hex(r[a].base) + ") and " + name + std::to_string(b) + " (" +
                        hex(r[b].base) + ") overlap";
                else
                    error = "the ranges at " + hex(r[a].base) + " and " + hex(r[b].base) + " overlap";
                return std::nullopt;
            }
    return r;
}

namespace {

// ports: s0..s(m-1), aclk, aresetn, m0..m(n-1). masters connect to the s
// ports, slaves to the m ports.
class crossbar_component : public clocked {
public:
    crossbar_component(const layout& l, int masters, int slaves, std::vector<range> map)
        : clocked(masters, masters + 1)
        , l(l)
        , map(std::move(map))
        , ms(masters)
        , ss(slaves)
    {
        clear();
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        for (int j = 0; j < (int)ss.size(); j++) {
            out.push_back({"m" + std::to_string(j) + " write owner", ss[j].w_owner < 0 ? sim::floating(4) : sim::known(ss[j].w_owner, 4), {}});
            out.push_back({"m" + std::to_string(j) + " read owner", ss[j].r_owner < 0 ? sim::floating(4) : sim::known(ss[j].r_owner, 4), {}});
        }
        for (int i = 0; i < (int)ms.size(); i++) {
            out.push_back({"s" + std::to_string(i) + " writing", sim::known(ms[i].busy_w, 1), {}});
            out.push_back({"s" + std::to_string(i) + " reading", sim::known(ms[i].busy_r, 1), {}});
        }
    }

private:
    static constexpr int to_error = -2;

    struct master_side {
        fifo aw, w, ar, b, r;
        bool busy_w = false, busy_r = false;
        // the slave its W beats go to, to_error or -1 when there are none
        int w_to = -1;
        // the error slave: a B to send, R beats to send
        bool err_b = false;
        int err_r = 0;
        value err_wid, err_rid;
    };
    struct slave_side {
        fifo aw, w, ar, b, r;
        int w_owner = -1, r_owner = -1;
        // the last master granted
        int rr_w = -1, rr_r = -1;
    };

    int port_s(int i) const { return i; }
    int port_m(int j) const { return 2 + (int)ms.size() + j; }

    bool idle(std::span<const value> ports) const override
    {
        for (int i = 0; i < (int)ms.size(); i++) {
            auto& m = ms[i];
            auto& p = ports[port_s(i)];
            if (!m.aw.empty() || !m.w.empty() || !m.ar.empty() || !m.b.empty() || !m.r.empty() || m.busy_w || m.busy_r || m.err_b || m.err_r) return false;
            if (high(p, l.awvalid) || high(p, l.wvalid) || high(p, l.arvalid)) return false;
        }
        for (int j = 0; j < (int)ss.size(); j++) {
            auto& sl = ss[j];
            auto& p = ports[port_m(j)];
            if (!sl.aw.empty() || !sl.w.empty() || !sl.ar.empty() || !sl.b.empty() || !sl.r.empty() || sl.w_owner >= 0 || sl.r_owner >= 0) return false;
            if (high(p, l.bvalid) || high(p, l.rvalid)) return false;
        }
        return true;
    }
    bool lite() const { return l.proto == protocol::lite; }

    void clear() override
    {
        for (auto& m : ms)
            m = {};
        for (auto& s : ss)
            s = {};
    }

    // the slave of an address, -1 if none has it
    int decode(uint64_t addr) const
    {
        for (int j = 0; j < (int)map.size(); j++)
            if (addr >= map[j].base && addr - map[j].base < map[j].size) return j;
        return -1;
    }

    // a master's round robin turn for slave j: the first after the last
    // granted that wants it
    template <class Wants>
    int pick(int last, Wants wants) const
    {
        int n = (int)ms.size();
        for (int k = 1; k <= n; k++) {
            int i = (last + k) % n;
            if (wants(i)) return i;
        }
        return -1;
    }

    void edge(std::span<const value> ports) override
    {
        // beats the masters and slaves hand over, pushed after the moves so
        // the buffers' ready as seen before the edge holds
        struct push {
            fifo* f;
            value v;
        };
        std::vector<push> pushes;
        auto take = [&](const value& port, const channel& c, fifo& f) {
            if (f.ready() && high(port, c.valid)) pushes.push_back({&f, port.slice(c.off, c.w)});
        };
        auto give = [&](const value& port, const channel& c, fifo& f) {
            if (f.head() && high(port, c.ready)) f.pop();
        };

        for (int i = 0; i < (int)ms.size(); i++) {
            auto& p = ports[port_s(i)];
            auto& m = ms[i];
            take(p, l.aw, m.aw);
            take(p, l.w, m.w);
            take(p, l.ar, m.ar);
            give(p, l.b, m.b);
            give(p, l.r, m.r);
        }
        for (int j = 0; j < (int)ss.size(); j++) {
            auto& p = ports[port_m(j)];
            auto& s = ss[j];
            give(p, l.aw, s.aw);
            give(p, l.w, s.w);
            give(p, l.ar, s.ar);
            take(p, l.b, s.b);
            take(p, l.r, s.r);
        }

        responses();
        writes();
        reads();

        for (auto& p : pushes)
            p.f->q.push_back(std::move(p.v));
    }

    // B and R back to the masters, from the slaves and the error slaves
    void responses()
    {
        for (int j = 0; j < (int)ss.size(); j++) {
            auto& s = ss[j];
            if (s.w_owner >= 0 && s.b.head() && ms[s.w_owner].b.ready() && ms[s.w_owner].w_to != j) {
                ms[s.w_owner].b.q.push_back(s.b.pop());
                ms[s.w_owner].busy_w = false;
                s.w_owner = -1;
            }
            if (s.r_owner >= 0 && s.r.head() && ms[s.r_owner].r.ready()) {
                auto beat = s.r.pop();
                bool last = lite() || field(beat, l.r, l.rlast);
                ms[s.r_owner].r.q.push_back(std::move(beat));
                if (last) {
                    ms[s.r_owner].busy_r = false;
                    s.r_owner = -1;
                }
            }
        }
        for (auto& m : ms) {
            if (m.err_b && m.b.ready()) {
                auto beat = zeros(l.b.w);
                set(beat, l.b, l.bid, m.err_wid);
                set(beat, l.b, l.bresp, decerr);
                m.b.q.push_back(std::move(beat));
                m.err_b = false;
                m.busy_w = false;
            }
            if (m.err_r > 0 && m.r.ready()) {
                auto beat = zeros(l.r.w);
                set(beat, l.r, l.rid, m.err_rid);
                set(beat, l.r, l.rresp, decerr);
                set(beat, l.r, l.rlast, m.err_r == 1);
                m.r.q.push_back(std::move(beat));
                if (--m.err_r == 0) m.busy_r = false;
            }
        }
    }

    void writes()
    {
        // W beats to where their AW went
        for (auto& m : ms) {
            if (m.w_to == -1 || !m.w.head()) continue;
            bool last = lite() || field(*m.w.head(), l.w, l.wlast);
            if (m.w_to == to_error) {
                m.w.pop();
                if (last) {
                    m.w_to = -1;
                    m.err_b = true;
                }
                continue;
            }
            auto& s = ss[m.w_to];
            if (!s.w.ready()) continue;
            s.w.q.push_back(m.w.pop());
            if (last) m.w_to = -1;
        }

        // AWs that no slave has get the error slave
        for (auto& m : ms) {
            if (m.busy_w || !m.aw.head() || decode(field(*m.aw.head(), l.aw, l.awaddr)) >= 0) continue;
            auto aw = m.aw.pop();
            m.err_wid = zeros(l.bid.w);
            if (l.awid.w) m.err_wid = aw.slice(l.awid.off - l.aw.off, l.awid.w);
            m.busy_w = true;
            m.w_to = to_error;
        }

        // a free slave takes the next master that wants it
        for (int j = 0; j < (int)ss.size(); j++) {
            auto& s = ss[j];
            if (s.w_owner >= 0 || !s.aw.ready()) continue;
            int i = pick(s.rr_w, [&](int i) { return !ms[i].busy_w && ms[i].aw.head() && decode(field(*ms[i].aw.head(), l.aw, l.awaddr)) == j; });
            if (i < 0) continue;
            s.aw.q.push_back(ms[i].aw.pop());
            s.w_owner = s.rr_w = i;
            ms[i].busy_w = true;
            ms[i].w_to = j;
        }
    }

    void reads()
    {
        for (auto& m : ms) {
            if (m.busy_r || !m.ar.head() || decode(field(*m.ar.head(), l.ar, l.araddr)) >= 0) continue;
            auto ar = m.ar.pop();
            m.err_rid = zeros(l.rid.w);
            if (l.arid.w) m.err_rid = ar.slice(l.arid.off - l.ar.off, l.arid.w);
            m.err_r = lite() ? 1 : (int)field(ar, l.ar, l.arlen) + 1;
            m.busy_r = true;
        }

        for (int j = 0; j < (int)ss.size(); j++) {
            auto& s = ss[j];
            if (s.r_owner >= 0 || !s.ar.ready()) continue;
            int i = pick(s.rr_r, [&](int i) { return !ms[i].busy_r && ms[i].ar.head() && decode(field(*ms[i].ar.head(), l.ar, l.araddr)) == j; });
            if (i < 0) continue;
            s.ar.q.push_back(ms[i].ar.pop());
            s.r_owner = s.rr_r = i;
            ms[i].busy_r = true;
        }
    }

    void outputs(std::span<value> ports) override
    {
        for (int i = 0; i < (int)ms.size(); i++) {
            auto& m = ms[i];
            drive d(l.total);
            d.sink(l.aw, m.aw.ready());
            d.sink(l.w, m.w.ready());
            d.sink(l.ar, m.ar.ready());
            d.source(l.b, m.b.head());
            d.source(l.r, m.r.head());
            ports[port_s(i)] = std::move(d.v);
        }
        for (int j = 0; j < (int)ss.size(); j++) {
            auto& s = ss[j];
            drive d(l.total);
            d.source(l.aw, s.aw.head());
            d.source(l.w, s.w.head());
            d.source(l.ar, s.ar.head());
            d.sink(l.b, s.b.ready());
            d.sink(l.r, s.r.ready());
            ports[port_m(j)] = std::move(d.v);
        }
    }

    layout l;
    std::vector<range> map;
    std::vector<master_side> ms;
    std::vector<slave_side> ss;
};

// the bytes of an axi ram, for the debugger's memory view
class ram_bytes : public sim::debug_interface {
public:
    explicit ram_bytes(size_t size)
        : bytes(size, 0)
    {
    }

    std::vector<sim::memory_space> memories() const override { return {{"ram", bytes.size()}}; }

    bool read_memory(int space, uint64_t address, std::span<uint8_t> out) const override
    {
        if (space != 0 || address + out.size() > bytes.size()) return false;
        std::copy_n(bytes.begin() + address, out.size(), out.begin());
        return true;
    }

    bool write_memory(int space, uint64_t address, std::span<const uint8_t> data) override
    {
        if (space != 0 || address + data.size() > bytes.size()) return false;
        std::copy(data.begin(), data.end(), bytes.begin() + address);
        return true;
    }

    std::vector<uint8_t> bytes;
    // from the files at offsets in bytes, put there at reset
    std::vector<io::firmware::segment> initial;
};

// ports: s, aclk, aresetn. one write and one read at a time, a beat per
// cycle. addresses wrap at the size.
class ram_component : public clocked {
public:
    ram_component(const layout& l, size_t size)
        : clocked(1, 2)
        , l(l)
        , mem(size)
    {
        clear();
    }

    sim::debug_interface* debug() override { return &mem; }
    ram_bytes& memory() { return mem; }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        out.push_back({"writing", sim::known(wr.active, 1), {}});
        out.push_back({"reading", sim::known(rd.active, 1), {}});
    }

private:
    struct burst {
        bool active = false;
        uint64_t addr = 0;
        int size = 0, kind = 1, len = 0, beat = 0;
        value id;
    };

    int bus_bytes() const { return l.wdata.w / 8; }

    void clear() override
    {
        aw = {}, w = {}, ar = {}, b = {}, r = {};
        wr = {}, rd = {};
        std::fill(mem.bytes.begin(), mem.bytes.end(), 0);
        for (auto& seg : mem.initial)
            std::copy(seg.bytes.begin(), seg.bytes.end(), mem.bytes.begin() + ptrdiff_t(seg.address));
    }

    // a burst from an AW or AR beat
    burst start(const value& beat, const channel& c, const sig& id, const sig& addr, const sig& len, const sig& size, const sig& kind) const
    {
        burst t;
        t.active = true;
        t.addr = field(beat, c, addr);
        t.id = id.w ? beat.slice(id.off - c.off, id.w) : value();
        if (l.proto == protocol::lite) {
            int full = 0;
            while ((1 << full) < bus_bytes())
                full++;
            t.size = full;
            return t;
        }
        t.len = (int)field(beat, c, len);
        t.size = (int)field(beat, c, size);
        t.kind = (int)field(beat, c, kind);
        return t;
    }

    // the bus-aligned address of a burst's current beat
    uint64_t lane_base(const burst& t) const
    {
        auto a = beat_address(t.addr, t.size, t.kind, t.len, t.beat);
        return a & ~uint64_t(bus_bytes() - 1);
    }

    void edge(std::span<const value> ports) override
    {
        auto& p = ports[0];
        // as seen before the edge
        bool aw_ready = aw.ready(), w_ready = w.ready(), ar_ready = ar.ready();
        if (b.head() && high(p, l.bready)) b.pop();
        if (r.head() && high(p, l.rready)) r.pop();

        // a write beat per cycle, the B once the last is in
        if (!wr.active && aw.head()) wr = start(aw.pop(), l.aw, l.awid, l.awaddr, l.awlen, l.awsize, l.awburst);
        if (wr.active && w.head() && b.ready()) {
            auto beat = w.pop();
            auto base = lane_base(wr);
            for (int k = 0; k < bus_bytes(); k++) {
                if (!high(beat, l.wstrb.off - l.w.off + k)) continue;
                mem.bytes[(base + k) % mem.bytes.size()] = (uint8_t)field(beat, l.wdata.off - l.w.off + 8 * k, 8);
            }
            bool last = l.proto == protocol::lite || field(beat, l.w, l.wlast) || wr.beat >= wr.len;
            wr.beat++;
            if (last) {
                auto resp = zeros(l.b.w);
                set(resp, l.b, l.bid, wr.id);
                set(resp, l.b, l.bresp, okay);
                b.q.push_back(std::move(resp));
                wr.active = false;
            }
        }

        if (!rd.active && ar.head()) rd = start(ar.pop(), l.ar, l.arid, l.araddr, l.arlen, l.arsize, l.arburst);
        if (rd.active && r.ready()) {
            auto beat = zeros(l.r.w);
            auto base = lane_base(rd);
            value data = zeros(l.rdata.w);
            for (int k = 0; k < bus_bytes(); k++)
                data.insert(8 * k, sim::known(mem.bytes[(base + k) % mem.bytes.size()], 8));
            set(beat, l.r, l.rid, rd.id);
            set(beat, l.r, l.rdata, data);
            set(beat, l.r, l.rresp, okay);
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
        drive d(l.total);
        d.sink(l.aw, aw.ready());
        d.sink(l.w, w.ready());
        d.sink(l.ar, ar.ready());
        d.source(l.b, b.head());
        d.source(l.r, r.head());
        ports[0] = std::move(d.v);
    }

    bool idle(std::span<const value> ports) const override
    {
        auto& p = ports[0];
        return aw.empty() && w.empty() && ar.empty() && b.empty() && r.empty() && !wr.active && !rd.active && !high(p, l.awvalid) && !high(p, l.wvalid) &&
            !high(p, l.arvalid);
    }

    layout l;
    ram_bytes mem;
    fifo aw, w, ar, b, r;
    burst wr, rd;
};

// ports: aclk, aresetn, m. one write or read at a time, started from the
// simulation's properties with interact
class master_component : public clocked {
public:
    explicit master_component(const layout& l)
        : clocked(0, 1)
        , l(l)
    {
        int full = 0;
        while ((1 << full) < l.wdata.w / 8)
            full++;
        size = full_size = full;
        clear();
    }

    bool interact(const std::string& action, int64_t v) override
    {
        if (action == "addr") addr = (uint64_t)v;
        else if (action == "data") data = (uint64_t)v;
        else if (action == "len") len = l.proto == protocol::lite ? 0 : (int)std::clamp<int64_t>(v, 0, 255);
        else if (action == "size") size = (int)std::clamp<int64_t>(v, 0, full_size);
        else if ((action == "write" || action == "read") && st == state::idle) {
            st = action == "write" ? state::aw : state::ar;
            beat = 0;
            beats = 0;
            changed();
            return true;
        }
        return false;
    }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        static const char* const names[] = {"idle", "address (write)", "data (write)", "response (write)", "address (read)", "data (read)"};
        out.push_back({"state", {}, names[(int)st]});
        out.push_back({"busy", sim::known(st != state::idle, 1), {}});
        out.push_back({"done", sim::known(done, 32), {}});
        out.push_back({"resp", sim::known(resp, 2), {}});
        out.push_back({"response", {}, resp_name(resp)});
        out.push_back({"beats", sim::known(beats, 9), {}});
        out.push_back({"rdata first", first_data, {}});
        out.push_back({"rdata last", last_data, {}});
    }

private:
    enum class state {
        idle,
        aw,
        w,
        b,
        ar,
        r
    };

    void clear() override
    {
        st = state::idle;
        beat = beats = 0;
        resp = 0;
        first_data = last_data = zeros(std::min(l.rdata.w, 64));
    }

    bool lite() const { return l.proto == protocol::lite; }

    value address_beat(const channel& c, const sig& a, const sig& ln, const sig& sz, const sig& burst) const
    {
        auto v = zeros(c.w);
        set(v, c, a, addr);
        set(v, c, ln, len);
        set(v, c, sz, size);
        set(v, c, burst, 1);
        return v;
    }

    value write_beat() const
    {
        auto v = zeros(l.w.w);
        // data + beat in every 64 bits
        auto word = data + (uint64_t)beat;
        value d = zeros(l.wdata.w);
        for (int bit = 0; bit < l.wdata.w; bit += 64)
            d.insert(bit, sim::known(word & sim::mask(std::min(64, l.wdata.w - bit)), std::min(64, l.wdata.w - bit)));
        set(v, l.w, l.wdata, d);
        // the lanes of this beat
        int bus = l.wdata.w / 8;
        auto a = beat_address(addr, size, 1, len, beat);
        int lo = int(a % bus);
        int hi = int((a & ~((uint64_t(1) << size) - 1)) % bus) + (1 << size);
        value strb = zeros(l.wstrb.w);
        for (int k = lo; k < hi && k < bus; k++)
            strb.insert(k, sim::known(1, 1));
        set(v, l.w, l.wstrb, strb);
        set(v, l.w, l.wlast, beat == len);
        return v;
    }

    void edge(std::span<const value> ports) override
    {
        auto& p = ports[2];
        switch (st) {
        case state::idle: break;
        case state::aw:
            if (high(p, l.awready)) st = state::w;
            break;
        case state::w:
            if (high(p, l.wready) && beat++ == len) st = state::b;
            break;
        case state::b:
            if (high(p, l.bvalid)) {
                resp = (int)field(p, l.bresp.off, l.bresp.w);
                done++;
                st = state::idle;
            }
            break;
        case state::ar:
            if (high(p, l.arready)) st = state::r;
            break;
        case state::r:
            if (high(p, l.rvalid)) {
                auto d = p.slice(l.rdata.off, std::min(l.rdata.w, 64));
                if (beats == 0) first_data = d;
                last_data = d;
                beats++;
                resp = (int)field(p, l.rresp.off, l.rresp.w);
                if (lite() || high(p, l.rlast)) {
                    done++;
                    st = state::idle;
                }
            }
            break;
        }
    }

    void outputs(std::span<value> ports) override
    {
        drive d(l.total);
        auto aw = address_beat(l.aw, l.awaddr, l.awlen, l.awsize, l.awburst);
        auto ar = address_beat(l.ar, l.araddr, l.arlen, l.arsize, l.arburst);
        auto w = write_beat();
        d.source(l.aw, st == state::aw ? &aw : nullptr);
        d.source(l.w, st == state::w ? &w : nullptr);
        d.source(l.ar, st == state::ar ? &ar : nullptr);
        d.sink(l.b, st == state::b);
        d.sink(l.r, st == state::r);
        ports[2] = std::move(d.v);
    }

    bool idle(std::span<const value>) const override { return st == state::idle; }

    layout l;
    int full_size = 0;
    // set with interact
    uint64_t addr = 0, data = 0;
    int len = 0, size = 0;

    state st = state::idle;
    int beat = 0, beats = 0, resp = 0;
    uint32_t done = 0;
    value first_data, last_data;
};


class crossbar : public primitive {
public:
    std::string kind() const override { return "axi_crossbar"; }
    std::string name() const override { return "AXI crossbar"; }
    std::string category() const override { return "AXI"; }
    std::string description() const override
    {
        return "connects AXI masters to slaves by address, a transaction per master and direction at a time, every channel registered";
    }

    std::vector<model::param_def> params() const override
    {
        auto r = axi::params();
        r.insert(r.begin() + 1, model::int_param("masters", "Masters", 2, 1, 16, "s ports, the masters connect to them"));
        r.insert(r.begin() + 2, model::int_param("slaves", "Slaves", 2, 1, 16, "m ports, the slaves connect to them"));
        r.push_back(model::text_param("map", "Address map", "",
            "base/size per slave, e.g. \"0x0000/4K, 0x1000/4K\". empty: the address space in equal slices. other addresses get DECERR"));
        return r;
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        auto w = usable(proto, widths_of(v));
        int masters = (int)model::get_int(v, "masters"), slaves = (int)model::get_int(v, "slaves");
        int total = make_layout(proto, w).total;

        std::vector<builder::box_pin> left, right;
        std::vector<std::pair<std::string, bool>> bundles;
        for (int i = 0; i < masters; i++) {
            auto name = "s" + std::to_string(i);
            left.push_back({name, direction::inout, total, "a master connects here", {}, true});
            bundles.push_back({name, true});
        }
        left.push_back(clock_pin());
        left.push_back(reset_pin());
        std::string error;
        auto map = parse_map(model::get_string(v, "map"), slaves, w.addr, error);
        for (int j = 0; j < slaves; j++) {
            auto name = "m" + std::to_string(j);
            std::string where = map ? " at 0x" + [&] {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%llX", (unsigned long long)(*map)[j].base);
                return std::string(buf);
            }() : std::string();
            right.push_back({name, direction::inout, total, "a slave connects here" + where, {}, true});
            bundles.push_back({name, false});
        }
        builder b;
        b.box("AXI " + std::to_string(masters) + "x" + std::to_string(slaves), left, right, {}, 6);
        return finish_ports(b, proto, w, bundles);
    }

    std::string check(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        if (auto e = check_widths(proto, widths_of(v)); !e.empty()) return e;
        std::string error;
        if (!parse_map(model::get_string(v, "map"), (int)model::get_int(v, "slaves"), (int)model::get_int(v, "ADDR_W"), error)) return error;
        return {};
    }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        auto w = usable(proto, widths_of(v));
        std::string error;
        auto map = parse_map(model::get_string(v, "map"), (int)model::get_int(v, "slaves"), w.addr, error);
        return std::make_unique<crossbar_component>(make_layout(proto, w), (int)model::get_int(v, "masters"), (int)model::get_int(v, "slaves"),
            map.value_or(std::vector<range>{}));
    }
};

// "fw.bin@0x80000000, Image@0x80400000": files and the addresses binaries
// go at, 0 without one
bool parse_images(const std::string& text, std::vector<std::pair<std::string, uint64_t>>& out, std::string& error)
{
    for (auto& part : model::split_fields(text)) {
        auto at = part.rfind('@');
        std::string name = part.substr(0, at);
        while (!name.empty() && std::isspace((unsigned char)name.back()))
            name.pop_back();
        uint64_t base = 0;
        if (at != std::string::npos) {
            std::string num = part.substr(at + 1);
            std::erase_if(num, [](char ch) { return std::isspace((unsigned char)ch); });
            auto n = parse_number(num);
            if (!n) {
                error = "'" + part + "' isn't file@address";
                return false;
            }
            base = *n;
        }
        if (name.empty()) {
            error = "'" + part + "' has no file";
            return false;
        }
        out.push_back({name, base});
    }
    return true;
}

class ram : public primitive {
public:
    std::string kind() const override { return "axi_ram"; }
    std::string name() const override { return "AXI RAM"; }
    std::string category() const override { return "AXI"; }
    std::string description() const override { return "memory with an AXI slave port: bursts, byte strobes, addresses wrap at its size"; }

    std::vector<model::param_def> params() const override
    {
        auto r = axi::params();
        r.push_back(model::int_param("size", "Size bits", 12, 4, 28, "the memory has 2^size bytes, e.g. 12 for 4 KiB, 27 for 128 MiB"));
        r.push_back(model::file_param("file", "Contents", "intel hex, s-records, elf or binary, relative to the library folder. addresses wrap at the size"));
        r.push_back(model::text_param("images", "Images", "",
            "more files, \"file@address\" separated by commas: binaries go at the address, elf and hex files where they say. addresses wrap at the size"));
        return r;
    }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        auto w = usable(proto, widths_of(v));
        int total = make_layout(proto, w).total;
        int size = (int)model::get_int(v, "size");
        std::string bytes = size >= 20 ? std::to_string(1 << (size - 20)) + " MiB" : size >= 10 ? std::to_string(1 << (size - 10)) + " KiB" : std::to_string(1 << size) + " B";
        builder b;
        b.box("RAM " + bytes, {{"s", direction::inout, total, "the master connects here", {}, true}, clock_pin(), reset_pin()}, {}, {}, 6);
        return finish_ports(b, proto, w, {{"s", true}});
    }

    std::string check(const model::param_values& v) const override
    {
        if (auto e = check_widths(protocol_of(v), widths_of(v)); !e.empty()) return e;
        std::vector<std::pair<std::string, uint64_t>> images;
        std::string error;
        if (!parse_images(model::get_string(v, "images"), images, error)) return error;
        return {};
    }

    std::unique_ptr<sim::component> create(const model::param_values&) const override { return nullptr; }

    std::unique_ptr<sim::component> create(const sim::create_info& info, std::string& error) const override
    {
        auto& v = info.params;
        auto proto = protocol_of(v);
        auto c = std::make_unique<ram_component>(make_layout(proto, usable(proto, widths_of(v))), size_t(1) << model::get_int(v, "size"));
        // the contents go where the ram sees their addresses, which wrap
        // at its size: an elf linked at 0x80000000 starts at 0
        auto load = [&](const std::string& name, uint64_t base) {
            std::filesystem::path p = name;
            if (p.is_relative()) p = info.dir / p;
            io::firmware f;
            if (!io::load_firmware(p, f, error, base)) return false;
            uint64_t size = c->memory().bytes.size();
            for (auto& seg : f.segments) {
                uint64_t at = seg.address % size;
                if (seg.bytes.size() > size - at) {
                    error = name + " doesn't fit in the ram at 0x" + [&] {
                        char buf[24];
                        std::snprintf(buf, sizeof(buf), "%llX", (unsigned long long)seg.address);
                        return std::string(buf);
                    }();
                    return false;
                }
                c->memory().initial.push_back({at, std::move(seg.bytes)});
            }
            return true;
        };
        auto file = model::get_string(v, "file");
        if (!file.empty() && !load(file, 0)) return nullptr;
        std::vector<std::pair<std::string, uint64_t>> images;
        if (!parse_images(model::get_string(v, "images"), images, error)) return nullptr;
        for (auto& [name, base] : images)
            if (!load(name, base)) return nullptr;
        c->reset();
        return c;
    }
};

class master : public primitive {
public:
    std::string kind() const override { return "axi_master"; }
    std::string name() const override { return "AXI test master"; }
    std::string category() const override { return "AXI"; }
    std::string description() const override { return "writes and reads over AXI when asked to in the simulation's properties"; }

    std::vector<model::param_def> params() const override { return axi::params(); }

    model::cell_interface build(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        auto w = usable(proto, widths_of(v));
        int total = make_layout(proto, w).total;
        builder b;
        b.box("master", {clock_pin(), reset_pin()}, {{"m", direction::inout, total, "connects to a slave or a crossbar", {}, true}}, {}, 6);
        return finish_ports(b, proto, w, {{"m", false}});
    }

    std::string check(const model::param_values& v) const override { return check_widths(protocol_of(v), widths_of(v)); }

    std::unique_ptr<sim::component> create(const model::param_values& v) const override
    {
        auto proto = protocol_of(v);
        return std::make_unique<master_component>(make_layout(proto, usable(proto, widths_of(v))));
    }
};

}

}

namespace logic::builtin {

void add_axi(model::primitive_registry& r)
{
    r.add_bundle(axi::bundle(axi::protocol::axi4));
    r.add_bundle(axi::bundle(axi::protocol::lite));
    r.add(std::make_unique<axi::master>());
    r.add(std::make_unique<axi::crossbar>());
    r.add(std::make_unique<axi::ram>());
}

}

