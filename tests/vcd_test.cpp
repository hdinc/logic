// vcd files, traces, search and sessions of the waveform library

#include "../src/wave/search.h"
#include "../src/wave/session.h"
#include "../src/wave/vcd.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

using namespace logic;
using namespace logic::wave;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const char* sample = R"($date today $end
$version a tool $end
$comment
  several lines
$end
$timescale 10 ns $end
$scope module top $end
$var wire 1 ! clk $end
$var wire 8 " data [7:0] $end
$var real 64 # level $end
$scope module sub $end
$var wire 1 ! clk_alias $end
$var wire 4 $ nib [3:0] $end
$var string 1 % state $end
$upscope $end
$upscope $end
$enddefinitions $end
#0
$dumpvars
0!
b1010 "
r0.5 #
bz $
sIDLE %
$end
#5
1!
b11111111 "
r-1.25 #
U$
#10
0!
bx "
sRUN %
#15
1!
b1 "
$dumpoff
$end
#20
$dumpon
0!
b10 "
)";

static void test_parse()
{
    auto set = parse_vcd(sample);
    CHECK(set.tb.exponent == -9);
    CHECK(set.begin == 0 && set.end == 200);
    CHECK(set.signals.size() == 6);
    CHECK(set.traces.size() == 5);

    auto clk = set.find_name("top.clk");
    auto alias = set.find_name("top.sub.clk_alias");
    CHECK(clk && alias && clk->trace == alias->trace);
    CHECK(clk && clk->leaf() == "clk" && clk->scope() == "top");
    auto data = set.find_name("top.data[7:0]");
    CHECK(data && data->width == 8 && data->type == kind::digital);
    auto level = set.find_name("top.level");
    CHECK(level && level->type == kind::real);
    auto nib = set.find_name("top.sub.nib[3:0]");
    auto state = set.find_name("top.sub.state");
    CHECK(state && state->type == kind::text);
    if (!clk || !data || !level || !nib || !state) return;

    // times are in 10 ns ticks of a ns timebase
    auto& c = set.trace_of(*clk);
    CHECK(c.size() == 5);
    if (c.size() != 5) return;
    CHECK(c.at(1) == 50 && c.value(1) == sim::known(1, 1));
    // $dumpoff makes everything x, the last value at a time counts
    CHECK(c.at(3) == 150 && c.value(3) == sim::unknown(1));
    CHECK(c.at(4) == 200 && c.value(4) == sim::known(0, 1));

    auto& d = set.trace_of(*data);
    CHECK(d.size() == 4);
    if (d.size() != 4) return;
    // 1010 extends with zeros, x with x
    CHECK(d.value(0) == sim::known(0b1010, 8));
    CHECK(d.value(1) == sim::known(0xFF, 8));
    CHECK(d.at(2) == 100 && d.value(2) == sim::unknown(8));
    // the 1 at 150 is x after $dumpoff, the same as before
    CHECK(d.at(3) == 200 && d.value(3) == sim::known(2, 8));

    auto& n = set.trace_of(*nib);
    CHECK(n.value(0) == sim::floating(4));
    // a scalar std_logic U on a vector extends as x
    CHECK(n.value(1) == sim::unknown(4));

    auto& l = set.trace_of(*level);
    CHECK(l.real(0) == 0.5 && l.real(1) == -1.25 && std::isnan(l.real(2)));
    CHECK(l.range(0, l.size(), false) == std::make_pair(-1.25, 0.5));

    auto& st = set.trace_of(*state);
    CHECK(st.size() == 2 && st.text(0) == "IDLE" && st.text(1) == "RUN");

    CHECK(format_time(50, set.tb) == "50 ns");
    CHECK(format_time(1500, set.tb) == "1.5 us");
}

static void test_errors()
{
    auto fails_on_line = [](const char* text, const char* line) {
        try {
            parse_vcd(text);
        } catch (std::runtime_error& e) {
            return std::string(e.what()).rfind(line, 0) == 0;
        }
        return false;
    };
    CHECK(fails_on_line("$timescale 1ns $end\n$enddefinitions $end\n#0\n1?\n", "line 4: unknown identifier ?"));
    CHECK(fails_on_line("$var wire 1 ! a $end\n$enddefinitions $end\n#5\n#3\n", "line 4: time goes backwards"));
    CHECK(fails_on_line("$timescale 3 ns $end\n", "line 1: unsupported timescale"));
    CHECK(fails_on_line("$var wire 1 ! a $end", "line 1: no $enddefinitions"));

    // fs, as ghdl writes
    auto set = parse_vcd("$timescale 1 fs $end $var wire 1 ! a $end $enddefinitions $end #1000000 1!");
    CHECK(set.tb.exponent == -15 && format_time(set.end, set.tb) == "1 ns");

    load_progress progress;
    progress.cancel = true;
    std::string big = "$var wire 1 ! a $end $enddefinitions $end\n";
    for (int k = 0; k < 100000; k++)
        big += "#" + std::to_string(k) + " " + std::to_string(k % 2) + "!\n";
    bool cancelled = false;
    try {
        parse_vcd(big, &progress);
    } catch (std::runtime_error&) {
        cancelled = true;
    }
    CHECK(cancelled);
}

static void test_round_trip()
{
    auto set = parse_vcd(sample);
    std::ostringstream out;
    write_vcd(out, set);
    auto again = parse_vcd(out.str());
    CHECK(again.tb == set.tb);
    CHECK(again.end == set.end);
    CHECK(again.signals.size() == set.signals.size());
    for (auto& sig : set.signals) {
        auto other = again.find_name(sig.name);
        CHECK(other);
        if (!other) continue;
        auto &a = set.trace_of(sig), &b = again.trace_of(*other);
        CHECK(other->width == sig.width && other->type == sig.type);
        // reals don't keep x
        size_t n = a.type() == kind::real ? 2 : a.size();
        CHECK(b.size() >= n);
        for (size_t i = 0; i < n && i < b.size(); i++)
            CHECK(a.at(i) == b.at(i) && format_change(a, i, format::hex) == format_change(b, i, format::hex));
    }

    // slash separated simulator names become scopes
    trace_set sim;
    sim.tb = {-12};
    sim.end = 30;
    sim.traces.emplace_back(kind::digital, 1);
    sim.traces[0].push(0, sim::known(0, 1));
    sim.traces[0].push(10, sim::known(1, 1));
    sim.signals.push_back({7, "counter/U1.clk", 1, kind::digital, 0});
    std::ostringstream o2;
    write_vcd(o2, sim);
    auto back = parse_vcd(o2.str());
    CHECK(back.find_name("counter.U1.clk") && back.end == 30 && back.tb.exponent == -12);
}

static void test_trace()
{
    trace t(kind::digital, 8);
    CHECK(t.push(0, sim::known(1, 8)));
    CHECK(!t.push(5, sim::known(1, 8)));
    CHECK(t.push(10, sim::known(2, 8)));
    // several changes at one time keep the last, a glitch back is no change
    CHECK(t.push(10, sim::known(3, 8)));
    CHECK(t.size() == 2 && t.value(1) == sim::known(3, 8));
    t.push(10, sim::known(1, 8));
    CHECK(t.size() == 1);
    for (int k = 1; k <= 3000; k++)
        t.push(k * 10, sim::known(k % 256, 8));
    CHECK(t.size() == 3000);
    t.drop_front(2500);
    CHECK(t.size() == 500 && t.at(0) == 25010 && t.value(0) == sim::known(2501 % 256, 8));
    CHECK(t.index_at(25015) == 0 && t.index_at(25000) == trace::npos);
    CHECK(t.first_after(25010) == 1 && t.first_after(30000) == trace::npos);
    auto r = t.range(0, t.size(), false);
    CHECK(r.first == 0 && r.second == 255);
    auto rs = t.range(0, t.size(), true);
    CHECK(rs.first == -128 && rs.second == 127);
    CHECK(t.number(0, true) == double(int8_t(2501 % 256)));

    trace wide(kind::digital, 70);
    auto v = sim::known(0, 70);
    v.set(69, sim::bit_state::one);
    wide.push(0, v);
    CHECK(wide.number(0, false) == std::ldexp(1.0, 69));
    CHECK(wide.number(0, true) == -std::ldexp(1.0, 69));
}

static void test_search()
{
    trace t(kind::digital, 4);
    // 0 at 0, 1 at 10, 3 at 20, 2 at 30, 6 at 40
    int values[] = {0, 1, 3, 2, 6};
    for (int k = 0; k < 5; k++)
        t.push(k * 10, sim::known(values[k], 4));
    CHECK(next_change(t, 0) == 10);
    CHECK(next_change(t, 10) == 20);
    CHECK(next_change(t, 40) == never);
    CHECK(prev_change(t, 40) == 30);
    CHECK(prev_change(t, 35) == 30);
    CHECK(prev_change(t, 10) == never);
    // bit 0: 0 1 1 0 0
    CHECK(next_change(t, 0, 0) == 10);
    CHECK(next_change(t, 10, 0) == 30);
    CHECK(next_change(t, 30, 0) == never);
    CHECK(prev_change(t, 40, 0) == 30);
    CHECK(prev_change(t, 30, 0) == 10);
    // bit 1: 0 0 1 1 1
    auto bit1 = bit_trace(t, 1);
    CHECK(bit1.size() == 2 && bit1.at(1) == 20);

    auto p = parse_pattern("2", kind::digital, 4, format::uint);
    CHECK(p && next_match(t, 0, *p) == 30 && prev_match(t, 35, *p) == 30 && next_match(t, 30, *p) == never);
    // bit 0 and 3 clear: matches from 0 and from 30 on
    auto wild = parse_pattern("0b??0", kind::digital, 4, format::hex);
    CHECK(wild && next_match(t, 0, *wild) == 30 && next_match(t, 30, *wild) == never);
    CHECK(prev_match(t, 45, *wild) == 30 && prev_match(t, 30, *wild) == 0);
    CHECK(parse_pattern("-1", kind::digital, 4, format::sint)->v == sim::known(15, 4));
    CHECK(parse_pattern("0xz", kind::digital, 4, format::hex)->v == sim::floating(4));
    CHECK(parse_pattern("0x1F", kind::digital, 4, format::hex) == std::nullopt);
    CHECK(parse_pattern("12q", kind::digital, 8, format::uint) == std::nullopt);
    auto big = parse_pattern("1180591620717411303423", kind::digital, 70, format::uint);
    CHECK(big && big->v.bits(0) == UINT64_MAX && big->v.bits(1) == 0x3F);

    trace r(kind::real, 64);
    r.push(0, 0.0);
    r.push(5, 2.5);
    auto rp = parse_pattern("2.5", kind::real, 64, format::hex);
    CHECK(rp && next_match(r, 0, *rp) == 5);
}

static void test_formats()
{
    auto v = sim::known(255, 8);
    CHECK(format_value(v, format::hex) == "0xFF");
    CHECK(format_value(v, format::uint) == "255");
    CHECK(format_value(v, format::sint) == "-1");
    CHECK(format_value(sim::known(128, 8), format::sint) == "-128");
    CHECK(format_value(v, format::binary) == "0b11111111");
    CHECK(format_value(v, format::octal) == "0o377");
    CHECK(format_value(sim::floating(8), format::hex) == "0xzz");
    CHECK(format_value(sim::unknown(8), format::uint) == "xxxxxxxx");
    auto wide = sim::known(0, 70);
    wide.set(69, sim::bit_state::one);
    CHECK(format_value(wide, format::uint) == "590295810358705651712");
    CHECK(format_value(wide, format::sint) == "-590295810358705651712");
    wide.set_word(0, UINT64_MAX, 0, 0);
    wide.set_word(1, 0x3F, 0, 0);
    CHECK(format_value(wide, format::uint) == "1180591620717411303423");
    CHECK(format_value(wide, format::sint) == "-1");
    CHECK(display_name("top/U1.Q", true) == "top/U1.Q");
    CHECK(display_name("top/U1.Q", false) == "Q");
    CHECK(display_name("top/clock", false) == "clock");

    CHECK(format_time(0, {-12}) == "0 s");
    CHECK(format_time(12500000, {-12}) == "12.5 us");
    CHECK(format_time(1000002000000, {-12}) == "1.000002 s");
    CHECK(format_time(3, {-15}) == "3 fs");
    CHECK(format_frequency(1000000, {-12}) == "1 MHz");
}

static void test_session()
{
    session s;
    s.file = "run.vcd";
    s.rows.push_back({"top.clk", std::nullopt, display::digital, 1, false, 0});
    s.rows.push_back({"top.level", format::sint, display::analog_linear, 3, true, 0xFF00FF00});
    s.cursor_a = 120;
    s.markers.push_back({"start", 40});
    s.view_start = 10;
    s.view_span = 500;
    s.default_format = format::uint;
    s.full_path = false;
    auto back = session_from_json(session_to_json(s));
    CHECK(back.file == "run.vcd");
    CHECK(back.rows.size() == 2);
    CHECK(back.rows[1].fmt == format::sint && back.rows[1].mode == display::analog_linear && back.rows[1].height == 3);
    CHECK(back.rows[1].expanded && back.rows[1].color == 0xFF00FF00);
    CHECK(!back.rows[0].fmt);
    CHECK(back.cursor_a == 120 && !back.cursor_b);
    CHECK(back.markers.size() == 1 && back.markers[0].name == "start" && back.markers[0].t == 40);
    CHECK(back.view_start == 10 && back.view_span == 500);
    CHECK(back.default_format == format::uint && !back.full_path);

    bool rejected = false;
    try {
        session_from_json("{\"rows\": []}");
    } catch (std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
}

// a 12 bit bundle: valid, data[8], a nested bundle s with x and y[2]
static void test_fields()
{
    trace_set set;
    set.tb = {-12};
    set.end = 40;
    set.traces.emplace_back(kind::digital, 12);
    auto& t = set.traces[0];
    t.push(0, sim::known(0, 12));
    t.push(10, sim::known(0xAB << 1 | 1, 12));          // valid, data AB
    t.push(20, sim::known(0xAB << 1 | 1 | 0b101 << 9, 12)); // s.x 1, s.y 2
    auto z = sim::known(1 | 0b101 << 9, 12);
    z.insert(1, sim::floating(8));
    t.push(30, z);                                       // data floating
    std::vector<field> fields{
        {"valid", 0, 1, 0, false},
        {"data", 1, 8, 0, false},
        {"s", 9, 3, 0, true},
        {"s.x", 9, 1, 1, false},
        {"s.y", 10, 2, 1, false},
    };
    set.signals.push_back({5, "top/bus", 12, kind::digital, 0, fields});

    // a field changes only where its bits do
    auto data = slice_trace(t, 1, 8);
    CHECK(data.size() == 3 && data.at(1) == 10 && data.value(1) == sim::known(0xAB, 8) && data.at(2) == 30 && data.fully_floating(2));
    auto x = slice_trace(t, 9, 1);
    CHECK(x.size() == 2 && x.at(1) == 20);

    CHECK(group_end(fields, 2) == 5 && group_end(fields, 0) == 1);
    // the vcd gets a signal per field in a scope named after the bundle
    auto flat = expand_fields(set);
    CHECK(flat.signals.size() == 4 && flat.find_name("top/bus.s.y") && flat.find_name("top/bus.valid"));
    if (auto y = flat.find_name("top/bus.s.y")) CHECK(y->width == 2 && flat.trace_of(*y).size() == 2 && flat.trace_of(*y).value(1) == sim::known(2, 2));
    std::ostringstream out;
    write_vcd(out, flat);
    auto back = parse_vcd(out.str());
    // the reader keeps the range in the name
    auto d = back.find_name("top.bus.data[7:0]");
    CHECK(d && d->width == 8 && back.find_name("top.bus.s.x"));
    if (d) CHECK(back.trace_of(*d).value(1) == sim::known(0xAB, 8));
}

int main()
{
    test_parse();
    test_errors();
    test_round_trip();
    test_trace();
    test_search();
    test_formats();
    test_session();
    test_fields();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
