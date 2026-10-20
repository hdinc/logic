/*
 * logic c model abi
 *
 * a plugin is a shared library (.so / .dll) that exports logic_plugin_entry.
 * it describes one or more models: parts that can be placed in a schematic
 * and are simulated by calling into the plugin, e.g. a microcontroller.
 *
 * values are 4-state: every bit is 0, 1, x (unknown) or z (not driven). a
 * value of width w is three arrays of logic_words(w) 64 bit words, least
 * significant word first: bits, x and z. a bit set in x or z has its bits
 * bit 0. bits above the width are 0.
 *
 * time is in picoseconds.
 *
 * threads: get_ports may be called from any thread. the functions of one
 * instance are called from one thread at a time, but different instances
 * may run on different threads. don't keep state outside the instance.
 *
 * compatibility: the host checks abi_version. new optional functions are
 * added at the end of logic_model_desc, struct_size tells the host which
 * ones a plugin knows.
 */

#ifndef LOGIC_MODEL_H
#define LOGIC_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGIC_MODEL_ABI_VERSION 1

#if defined(_WIN32)
#define LOGIC_EXPORT __declspec(dllexport)
#else
#define LOGIC_EXPORT __attribute__((visibility("default")))
#endif

#define LOGIC_NEVER UINT64_MAX

#define LOGIC_PS ((uint64_t)1)
#define LOGIC_NS ((uint64_t)1000)
#define LOGIC_US ((uint64_t)1000000)
#define LOGIC_MS ((uint64_t)1000000000)
#define LOGIC_S ((uint64_t)1000000000000)

static inline uint32_t logic_words(uint32_t width) { return (width + 63) / 64; }

/* ---- values ---- */

typedef struct logic_value {
    uint64_t* bits;
    uint64_t* x;
    uint64_t* z;
    uint32_t width;
} logic_value;

/* mask of the valid bits in word w */
static inline uint64_t logic_word_mask(uint32_t width, uint32_t w)
{
    uint32_t left = width - 64 * w;
    if (64 * w >= width) return 0;
    return left >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << left) - 1);
}

/* the value as a number, x and z bits count as 0 */
static inline uint64_t logic_get_u64(const logic_value* v) { return v->bits[0]; }

/* 1 if no bit is x or z */
static inline int logic_is_known(const logic_value* v)
{
    for (uint32_t w = 0; w < logic_words(v->width); w++)
        if (v->x[w] | v->z[w]) return 0;
    return 1;
}

/* 0, 1, 2 for x, 3 for z */
static inline int logic_get_bit(const logic_value* v, uint32_t i)
{
    uint64_t m = (uint64_t)1 << (i % 64);
    uint32_t w = i / 64;
    if (i >= v->width) return 3;
    if (v->x[w] & m) return 2;
    if (v->z[w] & m) return 3;
    return (v->bits[w] & m) ? 1 : 0;
}

/* sets the value to n (upper words 0), all bits known */
static inline void logic_set_u64(logic_value* v, uint64_t n)
{
    for (uint32_t w = 0; w < logic_words(v->width); w++) {
        v->bits[w] = w == 0 ? n & logic_word_mask(v->width, 0) : 0;
        v->x[w] = 0;
        v->z[w] = 0;
    }
}

/* every bit z: not driving */
static inline void logic_set_z(logic_value* v)
{
    for (uint32_t w = 0; w < logic_words(v->width); w++) {
        v->bits[w] = 0;
        v->x[w] = 0;
        v->z[w] = logic_word_mask(v->width, w);
    }
}

/* every bit x */
static inline void logic_set_x(logic_value* v)
{
    for (uint32_t w = 0; w < logic_words(v->width); w++) {
        v->bits[w] = 0;
        v->x[w] = logic_word_mask(v->width, w);
        v->z[w] = 0;
    }
}

/* bit i to 0, 1, 2 (x) or 3 (z) */
static inline void logic_set_bit(logic_value* v, uint32_t i, int state)
{
    uint64_t m = (uint64_t)1 << (i % 64);
    uint32_t w = i / 64;
    if (i >= v->width) return;
    v->bits[w] &= ~m;
    v->x[w] &= ~m;
    v->z[w] &= ~m;
    if (state == 1) v->bits[w] |= m;
    else if (state == 2) v->x[w] |= m;
    else if (state == 3) v->z[w] |= m;
}

/* ---- description ---- */

typedef enum logic_dir {
    LOGIC_INPUT = 0,
    LOGIC_OUTPUT = 1,
    LOGIC_INOUT = 2
} logic_dir;

typedef struct logic_port_desc {
    const char* name;
    logic_dir dir;
    uint32_t width;
    /* shown when hovering the pin, may be null */
    const char* description;
} logic_port_desc;

typedef enum logic_param_type {
    LOGIC_PARAM_INT = 0,
    LOGIC_PARAM_BOOL = 1,
    /* one of choices, the value is the string */
    LOGIC_PARAM_CHOICE = 2,
    LOGIC_PARAM_TEXT = 3,
    /* a file path, e.g. firmware, relative paths start at the library folder */
    LOGIC_PARAM_FILE = 4
} logic_param_type;

typedef struct logic_param_desc {
    const char* name;
    /* shown in the properties, the name if null */
    const char* label;
    logic_param_type type;
    /* default of int and bool params, and their range */
    int64_t def_int;
    int64_t min;
    int64_t max;
    /* default of choice, text and file params */
    const char* def_text;
    const char* const* choices;
    uint32_t choice_count;
    /* may be null */
    const char* description;
} logic_param_desc;

/* the value of a parameter for an instance: int and bool in i, the others
 * in s. every declared parameter has a value. */
typedef struct logic_param_value {
    const char* name;
    int64_t i;
    const char* s;
} logic_param_value;

typedef struct logic_params {
    const logic_param_value* values;
    uint32_t count;
} logic_params;

static inline const logic_param_value* logic_find_param(const logic_params* p, const char* name)
{
    for (uint32_t k = 0; k < p->count; k++) {
        const char *a = p->values[k].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        if (*a == *b) return &p->values[k];
    }
    return 0;
}

static inline int64_t logic_param_int(const logic_params* p, const char* name, int64_t def)
{
    const logic_param_value* v = logic_find_param(p, name);
    return v ? v->i : def;
}

static inline const char* logic_param_text(const logic_params* p, const char* name, const char* def)
{
    const logic_param_value* v = logic_find_param(p, name);
    return v && v->s ? v->s : def;
}

/* ---- host services ---- */

/* collects the ports from get_ports */
typedef struct logic_port_sink {
    void* data;
    void (*add)(void* data, const logic_port_desc* port);
    /* the parameters don't work, get_ports returns 0 after it */
    void (*error)(void* data, const char* message);
} logic_port_sink;

typedef enum logic_log_level {
    LOGIC_LOG_INFO = 0,
    LOGIC_LOG_WARNING = 1,
    LOGIC_LOG_ERROR = 2
} logic_log_level;

typedef struct logic_host {
    void* data;
    /* a message for the user, e.g. why create failed */
    void (*log)(void* data, logic_log_level level, const char* message);
    /* the folder relative file parameters start at, utf-8 */
    const char* library_dir;
} logic_host;

/* passed to eval */
typedef struct logic_context {
    /* current time */
    uint64_t now;
    /* 1 if eval was called because the wake time came */
    int woken;
    /* eval is called again at this time, LOGIC_NEVER for none. it holds
     * the pending request on entry, the model may change it. a time that
     * isn't in the future wakes after the current step. */
    uint64_t wake;
    /* set to a text (that stays valid) to pause the simulation after the
     * current point in time, e.g. "breakpoint". null on entry. */
    const char* pause;
} logic_context;

/* receives the state of an instance for debugging views */
typedef struct logic_inspect_sink {
    void* data;
    void (*value)(void* data, const char* name, const logic_value* v);
} logic_inspect_sink;

typedef struct logic_state_writer {
    void* data;
    void (*write)(void* data, const void* bytes, size_t size);
} logic_state_writer;

/* the contents of a file, e.g. firmware: bytes at addresses */
typedef struct logic_segment {
    uint64_t address;
    const uint8_t* bytes;
    uint64_t size;
} logic_segment;

typedef struct logic_image {
    const logic_segment* segments;
    uint32_t segment_count;
    int has_entry;
    uint64_t entry;
} logic_image;

typedef struct logic_memory_info {
    const char* name;
    /* in bytes */
    uint64_t size;
} logic_memory_info;

/* memories and execution of a processor for the debugger. every function
 * may be null. called from the simulation thread. */
typedef struct logic_debug_vtable {
    /* sizeof(logic_debug_vtable) of the plugin */
    uint32_t struct_size;
    uint32_t (*memory_count)(void* instance);
    /* returns 1 and fills info */
    int (*memory_info)(void* instance, uint32_t index, logic_memory_info* info);
    /* return 1 if the range is in the memory */
    int (*read_memory)(void* instance, uint32_t index, uint64_t address, uint8_t* out, uint64_t size);
    int (*write_memory)(void* instance, uint32_t index, uint64_t address, const uint8_t* data, uint64_t size);
    /* returns 1 and the program counter */
    int (*pc)(void* instance, uint64_t* pc);
    /* writes the instruction at address as text, returns its length in
     * bytes or 0 */
    uint32_t (*disassemble)(void* instance, uint64_t address, char* text, uint32_t text_size);
    /* the model pauses (logic_context.pause) before executing there */
    void (*set_breakpoint)(void* instance, uint64_t address, int on);
    /* the model pauses after the next instruction */
    void (*step_instruction)(void* instance);
} logic_debug_vtable;

/* ---- models ---- */

typedef struct logic_model_desc {
    /* sizeof(logic_model_desc) of the plugin */
    uint32_t struct_size;

    /* identifies the model in the plugin, stored in the files */
    const char* name;
    const char* description;
    /* group in the component bar, may be null */
    const char* category;

    const logic_param_desc* params;
    uint32_t param_count;

    /* adds the ports for the parameter values to the sink in their order.
     * returns 1, or 0 after calling sink->error. */
    int (*get_ports)(const logic_params* params, const logic_port_sink* sink);

    /* a new instance, null on failure after logging why with host. host
     * stays valid until destroy. */
    void* (*create)(const logic_params* params, const logic_host* host);
    void (*destroy)(void* instance);
    /* back to the state after create */
    void (*reset)(void* instance);

    /* called when an input changed, when ctx->wake is due and once after
     * reset. ports are in the order of get_ports. inputs and inouts hold
     * the values of their nets, outputs what was written last. set outputs
     * and inouts to what the instance drives, z where it doesn't drive. */
    void (*eval)(void* instance, logic_value* ports, logic_context* ctx);

    /* optional, may be null */
    void (*inspect)(void* instance, const logic_inspect_sink* sink);
    /* return 1 if supported */
    int (*save_state)(void* instance, const logic_state_writer* out);
    int (*load_state)(void* instance, const void* bytes, size_t size);

    /* ---- optional, the host reads them if struct_size includes them ---- */

    /* the contents of a LOGIC_PARAM_FILE parameter, read by the host
     * (intel hex, s-records, elf or binary). called after create, the model
     * keeps it over reset. returns 1 if it took it. */
    int (*load_image)(void* instance, const char* param, const logic_image* image, const logic_host* host);
    /* a user action, e.g. "press", "release", or "key" with a character.
     * returns 1 if the outputs may change. */
    int (*interact)(void* instance, const char* action, int64_t value);
    /* debugging, null if the model has nothing to debug */
    const logic_debug_vtable* debug;

    /* the model's symbol as json text, null for a generated box. either a
     * cell file as the program saves it (e.g. after editing the symbol of
     * the model's cell, from <workspace>/plugins/<plugin>/symbols), or just
     * a symbol whose pins name their port:
     *   {"shapes": [{"type": "line", "a": [-20, 10], "b": [20, 10]}, ...],
     *    "pins": [{"port": "clk", "pos": [-30, 0]}, ...]}
     * coordinates are y up, one grid is 10. every port needs a pin. */
    const char* symbol;
} logic_model_desc;

/* the part of logic_model_desc every plugin has */
#define LOGIC_MODEL_DESC_MIN_SIZE (offsetof(logic_model_desc, load_state) + sizeof(void*))

typedef struct logic_plugin {
    /* LOGIC_MODEL_ABI_VERSION the plugin was built with */
    uint32_t abi_version;
    const char* name;
    const logic_model_desc* models;
    uint32_t model_count;
} logic_plugin;

/* exported by every plugin. returns null if it can't work with the host's
 * abi version. */
typedef const logic_plugin* (*logic_plugin_entry_fn)(uint32_t host_abi_version);
#define LOGIC_PLUGIN_ENTRY_NAME "logic_plugin_entry"

#ifdef __cplusplus
}
#endif

#endif
