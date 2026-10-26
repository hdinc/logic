#include "component.h"
#include "cosim_protocol.h"

#include <cstring>

namespace logic::cosim {

namespace {

void put_u32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        b.push_back(uint8_t(v >> (8 * k)));
}

void put_u64(std::vector<uint8_t>& b, uint64_t v)
{
    for (int k = 0; k < 8; k++)
        b.push_back(uint8_t(v >> (8 * k)));
}

void put_value(std::vector<uint8_t>& b, const sim::value& v)
{
    auto d = v.data();
    for (int k = 0; k < 3 * v.words(); k++)
        put_u64(b, d[k]);
}

// reads little endian numbers and values from a payload
class reader {
public:
    explicit reader(const std::vector<uint8_t>& b)
        : b(b)
    {
    }

    bool u64(uint64_t& v)
    {
        if (pos + 8 > b.size()) return false;
        v = 0;
        for (int k = 0; k < 8; k++)
            v |= uint64_t(b[pos + k]) << (8 * k);
        pos += 8;
        return true;
    }

    bool u8(uint8_t& v)
    {
        if (pos + 1 > b.size()) return false;
        v = b[pos++];
        return true;
    }

    bool i32(int32_t& v)
    {
        if (pos + 4 > b.size()) return false;
        uint32_t u = 0;
        for (int k = 0; k < 4; k++)
            u |= uint32_t(b[pos + k]) << (8 * k);
        v = (int32_t)u;
        pos += 4;
        return true;
    }

    bool value(int width, sim::value& v)
    {
        v = sim::value(width, 0);
        auto d = v.data();
        for (int k = 0; k < 3 * v.words(); k++)
            if (!u64(d[k])) return false;
        v.normalize();
        return true;
    }

    std::string rest() const { return std::string(b.begin() + (long)pos, b.end()); }

private:
    const std::vector<uint8_t>& b;
    size_t pos = 1;
};

}

component::component(std::string n, std::vector<model::port> p, launch h, std::shared_ptr<plugin::message_log> l)
    : name(std::move(n))
    , ports(std::move(p))
    , how(std::move(h))
    , log(std::move(l))
{
    for (auto& port : ports) {
        driven.push_back(sim::floating(port.width));
        sent.push_back(sim::floating(port.width));
        state_values.push_back(sim::floating(port.width));
    }
}

std::unique_ptr<component> component::start(std::string name, std::vector<model::port> ports, launch how, std::shared_ptr<plugin::message_log> log,
    std::string& error)
{
    std::unique_ptr<component> c(new component(std::move(name), std::move(ports), std::move(how), std::move(log)));
    if (!c->connect(error)) return nullptr;
    return c;
}

component::~component()
{
    shutdown();
}

bool component::connect(std::string& error)
{
    listener l;
    if (!l.open(error)) return false;

    auto env = how.env;
    env.push_back({LOGIC_COSIM_ENV_MODE, "run"});
    env.push_back({LOGIC_COSIM_ENV_PORT, std::to_string(l.port())});
    env.push_back({LOGIC_COSIM_ENV_TOP, how.testbench});

    process = std::make_unique<hdl::child_process>();
    if (!process->start(how.args, how.dir, env, error)) return false;
    // the simulator's output ($display and errors) goes to the log
    output_reader = std::thread([p = process.get(), log = log, n = name] {
        p->read_output([&](const std::string& line) { log->add(n + ": " + line); });
    });

    // a simulator that fails to start ends instead of connecting
    for (int waited = 0; !conn && waited < timeout_ms && process->running(); waited += 100)
        conn = l.accept(100);
    // it may have connected and ended right away
    if (!conn) conn = l.accept(0);
    if (!conn) {
        error = "the simulator didn't connect, see the messages";
        shutdown();
        return false;
    }

    std::vector<uint8_t> init;
    put_u32(init, LOGIC_COSIM_VERSION);
    put_u32(init, (uint32_t)ports.size());
    for (auto& p : ports) {
        init.push_back(p.dir == model::direction::input ? 0 : p.dir == model::direction::output ? 1 : 2);
        put_u32(init, (uint32_t)p.width);
        init.push_back(uint8_t(p.name.size()));
        init.push_back(uint8_t(p.name.size() >> 8));
        init.insert(init.end(), p.name.begin(), p.name.end());
    }

    uint8_t type;
    std::vector<uint8_t> payload;
    if (!send(LOGIC_COSIM_INIT, init) || !receive(type, payload)) {
        error = "the simulator didn't answer";
        shutdown();
        return false;
    }
    if (type == LOGIC_COSIM_ERROR) {
        error = reader(payload).rest();
        shutdown();
        return false;
    }
    alive = true;
    // the first state at time 0
    if (type != LOGIC_COSIM_READY || !read_state()) {
        error = "the simulator didn't start";
        shutdown();
        return false;
    }
    driven = state_values;
    sent_any = false;
    pending = false;
    return true;
}

void component::shutdown()
{
    if (conn) {
        send(LOGIC_COSIM_QUIT, {});
        conn.reset();
    }
    if (process) process->kill();
    if (output_reader.joinable()) output_reader.join();
    if (process) process->wait();
    process.reset();
    alive = false;
}

bool component::send(uint8_t type, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> m;
    put_u32(m, (uint32_t)payload.size() + 1);
    m.push_back(type);
    m.insert(m.end(), payload.begin(), payload.end());
    return conn && conn->send(m.data(), m.size());
}

bool component::receive(uint8_t& type, std::vector<uint8_t>& payload)
{
    uint8_t len_bytes[4];
    if (!conn || !conn->receive(len_bytes, 4, timeout_ms)) return false;
    uint32_t len = len_bytes[0] | len_bytes[1] << 8 | len_bytes[2] << 16 | uint32_t(len_bytes[3]) << 24;
    if (len < 1 || len > (1u << 28)) return false;
    payload.resize(len);
    if (!conn->receive(payload.data(), len, timeout_ms)) return false;
    type = payload[0];
    return true;
}

bool component::read_state()
{
    uint8_t type;
    std::vector<uint8_t> payload;
    if (!receive(type, payload)) {
        fail("the simulator stopped answering");
        return false;
    }
    if (type == LOGIC_COSIM_ERROR) {
        fail(reader(payload).rest());
        return false;
    }
    if (type != LOGIC_COSIM_STATE) {
        fail("unexpected message from the simulator");
        return false;
    }

    reader r(payload);
    uint64_t t;
    uint8_t finished;
    bool ok = r.u64(t) && r.u8(finished);
    for (size_t k = 0; ok && k < ports.size(); k++)
        if (ports[k].dir != model::direction::input) ok = r.value(ports[k].width, state_values[k]);

    if (!ok) {
        fail("short state message from the simulator");
        return false;
    }
    state_time = t;
    state_finished = finished != 0;
    if (state_finished) {
        log->add(name + ": the simulation has finished, its outputs keep their values");
        // what it drove at the end
        driven = state_values;
        pending = false;
        alive = false;
        conn.reset();
    }
    return true;
}

void component::fail(const std::string& why)
{
    if (!alive) return;
    log->add(name + ": " + why);
    alive = false;
    conn.reset();
    if (process) process->kill();
}

void component::eval(std::span<sim::value> values, sim::context& ctx)
{
    auto now = ctx.now();

    if (alive && ctx.woken() && pending && state_time <= now) {
        // the time it stopped at when running ahead has come
        driven = state_values;
        pending = false;
        ctx.wake_at(now);
    } else if (alive && ctx.woken() && !pending) {
        // everything at this time settled: run until the next event
        // elsewhere, or until the design changes an output
        auto h = ctx.horizon();
        auto until = h == sim::never ? now + idle_step : h;
        if (until <= now) {
            // the simulation goes no further now, inputs may still change at
            // this time: run when time goes on
            ctx.wake_at(now + 1);
            goto write;
        }
        std::vector<uint8_t> p;
        put_u64(p, until);
        if (send(LOGIC_COSIM_RUN, p) && read_state()) {
            if (state_finished) {
                // driven is the final state
            } else if (state_time <= now) {
                if (state_values != driven) {
                    // changed later at this time
                    driven = state_values;
                    ctx.wake_at(now);
                } else if (until != sim::never) {
                    // no progress, e.g. the simulator's time unit is longer
                    // than the step: again when something else happens
                    ctx.wake_at(until);
                }
            } else {
                pending = true;
                ctx.wake_at(state_time);
            }
        } else if (alive) {
            fail("the simulator stopped answering");
        }
    } else if (alive && !ctx.woken()) {
        // inputs changed: the simulator takes them at its time, which is now
        // unless it ran ahead
        bool changed = !sent_any;
        for (size_t k = 0; k < ports.size() && !changed; k++)
            changed = ports[k].dir != model::direction::output && values[k] != sent[k];
        if (changed) {
            std::vector<uint8_t> p;
            put_u64(p, now);
            for (size_t k = 0; k < ports.size(); k++) {
                if (ports[k].dir == model::direction::output) continue;
                put_value(p, values[k]);
                sent[k] = values[k];
            }
            sent_any = true;
            if (send(LOGIC_COSIM_SET, p) && read_state()) {
                if (!state_finished) driven = state_values;
                pending = false;
            } else if (alive) {
                fail("the simulator stopped answering");
            }
        }
        // run ahead once this time settled
        if (alive) ctx.wake_at(now);
    }

write:
    for (size_t k = 0; k < ports.size(); k++)
        if (ports[k].dir != model::direction::input) values[k] = driven[k];
}

void component::reset()
{
    shutdown();
    for (size_t k = 0; k < ports.size(); k++) {
        driven[k] = sim::floating(ports[k].width);
        sent[k] = sim::floating(ports[k].width);
    }
    std::string error;
    if (!connect(error)) log->add(name + ": can't start the simulator again: " + error);
}

}
