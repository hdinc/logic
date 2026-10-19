#include "sim_session.h"

namespace logic::editor {

sim_session::sim_session(const model::workspace& ws, const model::cell& c, const sim::component_factory& factory)
    : run(std::make_unique<sim::simulator>(ws, c, factory))
    , top(c.id)
{
}

void sim_session::watch(const sim::scope* s, int instance)
{
    watch_scope = s;
    watch_instance = instance;
    std::lock_guard lk(watch_state->m);
    watch_state->data = {};
}

void sim_session::refresh_watch(int memory, uint64_t address, size_t bytes, std::optional<uint64_t> from, int lines)
{
    if (!watch_scope || watch_instance < 0) return;
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(watch_state->m);
        // one request at a time, not too often while running
        if (watch_state->pending) return;
        if (run.running() && now - last_refresh < std::chrono::milliseconds(100)) return;
        watch_state->pending = true;
    }
    last_refresh = now;

    run.query([state = watch_state, s = watch_scope, k = watch_instance, memory, address, bytes, from, lines](sim::simulator& sim) {
        watch_data d;
        d.memory = memory;
        d.memory_address = address;
        if (auto c = sim.component_of(*s, k)) {
            d.valid = true;
            c->inspect(d.items);
            if (auto dbg = c->debug()) {
                d.debug = true;
                d.memories = dbg->memories();
                d.pc = dbg->pc();
                d.breakpoints = dbg->breakpoints();
                if (memory >= 0 && memory < (int)d.memories.size() && address < d.memories[memory].size) {
                    d.bytes.resize(std::min<uint64_t>(bytes, d.memories[memory].size - address));
                    if (!dbg->read_memory(memory, address, d.bytes)) d.bytes.clear();
                }
                auto at = from ? from : d.pc;
                for (int n = 0; at && n < lines; n++) {
                    std::string text;
                    int len = dbg->disassemble(*at, text);
                    if (len <= 0) break;
                    d.disassembly.push_back({*at, len, text});
                    *at += len;
                }
            }
        }
        std::lock_guard lk(state->m);
        state->data = std::move(d);
        state->pending = false;
    });
}

watch_data sim_session::watched() const
{
    std::lock_guard lk(watch_state->m);
    return watch_state->data;
}

void sim_session::debug(std::function<void(sim::debug_interface&)> f)
{
    if (!watch_scope || watch_instance < 0) return;
    run.post([s = watch_scope, k = watch_instance, f = std::move(f)](sim::simulator& sim) {
        if (auto c = sim.component_of(*s, k))
            if (auto d = c->debug()) f(*d);
    });
}

void sim_session::interact(const sim::scope* s, int instance, std::string action, int64_t value)
{
    run.post([s, instance, action = std::move(action), value](sim::simulator& sim) { sim.interact(*s, instance, action, value); });
}

bool sim_session::uses(model::id cell) const
{
    auto& c = run.sim().cells();
    return std::find(c.begin(), c.end(), cell) != c.end();
}

}
