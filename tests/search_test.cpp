// tests for the search helpers: ranking matches and the recent list.
// no imgui context is needed.

#include "../src/ui/search.h"

#include <cstdio>

using namespace logic;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

static int rank(const char* name, const char* text)
{
    auto m = ui::match_text(name, text);
    return m ? m->rank : -1;
}

static void test_match()
{
    // case is ignored, the start of the name ranks first
    CHECK(rank("AND", "and") == 0);
    CHECK(rank("NAND", "and") == 2);
    CHECK(rank("NAND", "xor") == -1);
    CHECK(rank("AND", "andx") == -1);

    // word starts: after a space, punctuation, a digit change, camel case
    CHECK(rank("Bit extender", "ext") == 1);
    CHECK(rank("Tri-state buffer", "state") == 1);
    CHECK(rank("seg7_decoder", "dec") == 1);
    CHECK(rank("7-segment display", "seg") == 1);
    CHECK(rank("mos6502", "6502") == 1);
    CHECK(rank("bitExtender", "ext") == 1);
    CHECK(rank("Multiplexer", "plex") == 2);

    // the best place is chosen and its position kept
    auto m = ui::match_text("Mux out mux", "mux");
    CHECK(m && m->rank == 0 && m->pos == 0);
    m = ui::match_text("demux mux", "mux");
    CHECK(m && m->rank == 1 && m->pos == 6);
    m = ui::match_text("Register", "");
    CHECK(m && m->rank == 0 && m->pos == 0);
}

static void test_recent()
{
    ui::recent_list r(3);
    r.use(1), r.use(2), r.use(1);
    CHECK((r.items() == std::vector<uint64_t>{1, 2}));
    r.use(3), r.use(4);
    CHECK((r.items() == std::vector<uint64_t>{4, 3, 1}));
    CHECK(r.to_string() == "4,3,1");

    ui::recent_list s(3);
    s.from_string("7,x,8,,7,18446744073709551615,9");
    CHECK((s.items() == std::vector<uint64_t>{7, 8, 18446744073709551615ull}));
    s.from_string("");
    CHECK(s.items().empty());
}

int main()
{
    test_match();
    test_recent();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
