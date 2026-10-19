/* counter written in plain c: clk, rst, en -> q. counts on the rising edge
 * of clk while en is 1, rst (synchronous) sets it to 0. */

#include "logic_model.h"

#include <stdlib.h>
#include <string.h>

typedef struct counter {
    uint32_t width;
    uint64_t count;
    int unknown; /* the count is x, e.g. after an x on rst */
    int prev_clk;
} counter;

static const logic_param_desc counter_params[] = {
    {"width", "Bit width", LOGIC_PARAM_INT, 8, 1, 64, 0, 0, 0, "bits of q"},
};

static uint32_t width_of(const logic_params* p)
{
    return (uint32_t)logic_param_int(p, "width", 8);
}

static int counter_ports(const logic_params* p, const logic_port_sink* s)
{
    logic_port_desc ports[] = {
        {"clk", LOGIC_INPUT, 1, "counts on the rising edge"},
        {"rst", LOGIC_INPUT, 1, "1 sets the count to 0 on the next edge"},
        {"en", LOGIC_INPUT, 1, "counts while 1, not connected counts too"},
        {"q", LOGIC_OUTPUT, width_of(p), "the count"},
    };
    for (size_t k = 0; k < sizeof(ports) / sizeof(ports[0]); k++)
        s->add(s->data, &ports[k]);
    return 1;
}

static void counter_reset(void* i)
{
    counter* c = (counter*)i;
    c->count = 0;
    c->unknown = 0;
    c->prev_clk = 3;
}

static void* counter_create(const logic_params* p, const logic_host* h)
{
    counter* c = (counter*)calloc(1, sizeof(counter));
    (void)h;
    if (!c) return 0;
    c->width = width_of(p);
    counter_reset(c);
    return c;
}

static void counter_destroy(void* i)
{
    free(i);
}

static void counter_eval(void* i, logic_value* ports, logic_context* ctx)
{
    counter* c = (counter*)i;
    int clk = logic_get_bit(&ports[0], 0);
    int rst = logic_get_bit(&ports[1], 0);
    int en = logic_get_bit(&ports[2], 0);
    (void)ctx;

    if (c->prev_clk == 0 && clk == 1) {
        if (rst == 1) {
            c->count = 0;
            c->unknown = 0;
        } else if (rst == 2 || en == 2) {
            c->unknown = 1;
        } else if (en != 0) {
            /* z (not connected) counts */
            c->count = (c->count + 1) & logic_word_mask(c->width, 0);
        }
    }
    c->prev_clk = clk;

    if (c->unknown)
        logic_set_x(&ports[3]);
    else
        logic_set_u64(&ports[3], c->count);
}

static void counter_inspect(void* i, const logic_inspect_sink* s)
{
    counter* c = (counter*)i;
    uint64_t bits = c->unknown ? 0 : c->count, x = c->unknown ? logic_word_mask(c->width, 0) : 0, z = 0;
    logic_value v = {&bits, &x, &z, c->width};
    s->value(s->data, "count", &v);
}

static int counter_save(void* i, const logic_state_writer* w)
{
    counter* c = (counter*)i;
    w->write(w->data, c, sizeof(counter));
    return 1;
}

static int counter_load(void* i, const void* bytes, size_t size)
{
    counter* c = (counter*)i;
    if (size != sizeof(counter) || ((const counter*)bytes)->width != c->width) return 0;
    memcpy(c, bytes, sizeof(counter));
    return 1;
}

const logic_model_desc example_counter = {
    sizeof(logic_model_desc),
    "counter",
    "counts clock edges",
    "Examples",
    counter_params,
    1,
    counter_ports,
    counter_create,
    counter_destroy,
    counter_reset,
    counter_eval,
    counter_inspect,
    counter_save,
    counter_load,
};
