// c++ convenience on top of logic_model.h: a model is a class, make_model
// fills in the c function table.
//
//   struct my_model : logic_sdk::instance {
//       my_model(const logic_params& p, const logic_host& h);
//       void eval(logic_value* ports, logic_context& ctx) override;
//   };
//   static int my_ports(const logic_params* p, const logic_port_sink* s);
//   static const logic_model_desc desc = logic_sdk::make_model<my_model>("my_model", "what it does", "Category", params, n, my_ports);
//
// constructors may throw, the message is logged and create returns null.

#pragma once

#include "logic_model.h"

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

namespace logic_sdk {

class instance {
public:
    virtual ~instance() = default;
    virtual void reset() {}
    virtual void eval(logic_value* ports, logic_context& ctx) = 0;
    virtual void inspect(const logic_inspect_sink&) {}
    virtual bool save_state(const logic_state_writer&) { return false; }
    virtual bool load_state(const void*, size_t) { return false; }

    // see logic_model_desc
    virtual bool load_image(const char* /*param*/, const logic_image& /*image*/, const logic_host& /*host*/) { return false; }
    virtual bool interact(const char* /*action*/, int64_t /*value*/) { return false; }

    // debugging, see logic_debug_vtable. a model with memories or a program
    // counter overrides these.
    virtual std::vector<logic_memory_info> memories() { return {}; }
    virtual bool read_memory(uint32_t, uint64_t, uint8_t*, uint64_t) { return false; }
    virtual bool write_memory(uint32_t, uint64_t, const uint8_t*, uint64_t) { return false; }
    virtual bool pc(uint64_t&) { return false; }
    // the length of the instruction, 0 if unknown
    virtual uint32_t disassemble(uint64_t, std::string&) { return 0; }
    virtual void set_breakpoint(uint64_t, bool) {}
    virtual void step_instruction() {}
};

// adds a port to the sink
inline void add_port(const logic_port_sink* s, const char* name, logic_dir dir, uint32_t width, const char* description = nullptr)
{
    logic_port_desc d{name, dir, width, description};
    s->add(s->data, &d);
}

inline logic_param_desc int_param(const char* name, const char* label, int64_t def, int64_t min, int64_t max, const char* description = nullptr)
{
    return {name, label, LOGIC_PARAM_INT, def, min, max, nullptr, nullptr, 0, description};
}

inline logic_param_desc bool_param(const char* name, const char* label, bool def, const char* description = nullptr)
{
    return {name, label, LOGIC_PARAM_BOOL, def ? 1 : 0, 0, 1, nullptr, nullptr, 0, description};
}

inline logic_param_desc text_param(const char* name, const char* label, const char* def, const char* description = nullptr)
{
    return {name, label, LOGIC_PARAM_TEXT, 0, 0, 0, def, nullptr, 0, description};
}

inline logic_param_desc file_param(const char* name, const char* label, const char* description = nullptr)
{
    return {name, label, LOGIC_PARAM_FILE, 0, 0, 0, "", nullptr, 0, description};
}

template <size_t N>
logic_param_desc choice_param(const char* name, const char* label, const char* const (&choices)[N], const char* def, const char* description = nullptr)
{
    return {name, label, LOGIC_PARAM_CHOICE, 0, 0, 0, def, choices, (uint32_t)N, description};
}

// writes plain data for save_state
template <class T>
void write(const logic_state_writer& w, const T& v)
{
    w.write(w.data, &v, sizeof(T));
}

// forwards the debug table to the instance
inline const logic_debug_vtable* debug_vtable()
{
    static const logic_debug_vtable v = [] {
        logic_debug_vtable t{};
        t.struct_size = sizeof(logic_debug_vtable);
        t.memory_count = [](void* i) { return (uint32_t)static_cast<instance*>(i)->memories().size(); };
        t.memory_info = [](void* i, uint32_t k, logic_memory_info* out) {
            auto m = static_cast<instance*>(i)->memories();
            if (k >= m.size()) return 0;
            *out = m[k];
            return 1;
        };
        t.read_memory = [](void* i, uint32_t k, uint64_t a, uint8_t* out, uint64_t n) { return static_cast<instance*>(i)->read_memory(k, a, out, n) ? 1 : 0; };
        t.write_memory = [](void* i, uint32_t k, uint64_t a, const uint8_t* d, uint64_t n) {
            return static_cast<instance*>(i)->write_memory(k, a, d, n) ? 1 : 0;
        };
        t.pc = [](void* i, uint64_t* out) { return static_cast<instance*>(i)->pc(*out) ? 1 : 0; };
        t.disassemble = [](void* i, uint64_t a, char* text, uint32_t size) {
            std::string s;
            auto n = static_cast<instance*>(i)->disassemble(a, s);
            if (size > 0) {
                auto len = std::min<size_t>(s.size(), size - 1);
                s.copy(text, len);
                text[len] = 0;
            }
            return n;
        };
        t.set_breakpoint = [](void* i, uint64_t a, int on) { static_cast<instance*>(i)->set_breakpoint(a, on != 0); };
        t.step_instruction = [](void* i) { static_cast<instance*>(i)->step_instruction(); };
        return t;
    }();
    return &v;
}

template <class T>
logic_model_desc make_model(const char* name, const char* description, const char* category, const logic_param_desc* params, uint32_t param_count,
    int (*get_ports)(const logic_params*, const logic_port_sink*))
{
    logic_model_desc d{};
    d.struct_size = sizeof(logic_model_desc);
    d.name = name;
    d.description = description;
    d.category = category;
    d.params = params;
    d.param_count = param_count;
    d.get_ports = get_ports;
    d.create = [](const logic_params* p, const logic_host* h) -> void* {
        try {
            return static_cast<instance*>(new T(*p, *h));
        } catch (const std::exception& e) {
            if (h->log) h->log(h->data, LOGIC_LOG_ERROR, e.what());
            return nullptr;
        }
    };
    d.destroy = [](void* i) { delete static_cast<instance*>(i); };
    d.reset = [](void* i) { static_cast<instance*>(i)->reset(); };
    d.eval = [](void* i, logic_value* ports, logic_context* ctx) { static_cast<instance*>(i)->eval(ports, *ctx); };
    d.inspect = [](void* i, const logic_inspect_sink* s) { static_cast<instance*>(i)->inspect(*s); };
    d.save_state = [](void* i, const logic_state_writer* w) { return static_cast<instance*>(i)->save_state(*w) ? 1 : 0; };
    d.load_state = [](void* i, const void* b, size_t n) { return static_cast<instance*>(i)->load_state(b, n) ? 1 : 0; };
    d.load_image = [](void* i, const char* param, const logic_image* image, const logic_host* host) {
        return static_cast<instance*>(i)->load_image(param, *image, *host) ? 1 : 0;
    };
    d.interact = [](void* i, const char* action, int64_t value) { return static_cast<instance*>(i)->interact(action, value) ? 1 : 0; };
    d.debug = debug_vtable();
    return d;
}

// a value of up to 64 bits for inspect sinks
struct small_value {
    uint64_t bits = 0, x = 0, z = 0;
    logic_value view(uint32_t width) { return {&bits, &x, &z, width}; }
};

}
