#include "manager.h"

#include <set>

namespace logic::hdl {

namespace fs = std::filesystem;

namespace {

// the last lines of a build are kept
constexpr size_t max_log_lines = 2000;

// a backend that can simulate the language
const model::hdl_impl* supported(const model::cell& c)
{
    auto h = std::get_if<model::hdl_impl>(&c.impl);
    return h && model::supports(h->backend, h->language) ? h : nullptr;
}

}

manager::manager(const model::workspace& w, const plugin::registry& p, fs::path root)
    : ws(w)
    , plugins(p)
    , cache_root(std::move(root))
    , log(std::make_shared<plugin::message_log>())
{
    worker = std::thread([this] { loop(); });
}

manager::~manager()
{
    {
        std::lock_guard lk(m);
        quit = true;
        queue.clear();
    }
    cv.notify_all();
    worker.join();
}

const verilator_tools& manager::verilator() const
{
    std::call_once(verilator_once, [this] { verilator_t = find_verilator(); });
    return verilator_t;
}

const icarus_tools& manager::icarus() const
{
    std::call_once(icarus_once, [this] { icarus_t = find_icarus(); });
    return icarus_t;
}

const ghdl_tools& manager::ghdl() const
{
    std::call_once(ghdl_once, [this] { ghdl_t = find_ghdl(); });
    return ghdl_t;
}

fs::path manager::build_path(const job& j) const
{
    switch (j.impl.backend) {
    case model::hdl_backend::verilator: return verilator_plugin_path(cache_root, verilator_build_key(verilator(), j));
    case model::hdl_backend::icarus: return icarus_design_path(cache_root, icarus_build_key(icarus(), j));
    case model::hdl_backend::ghdl: return ghdl_work_path(cache_root, ghdl_build_key(ghdl(), j));
    }
    return {};
}

std::string manager::tool_status(model::hdl_backend b, bool& ok) const
{
    if (b == model::hdl_backend::verilator) {
        auto& t = verilator();
        ok = t.ok();
        return ok ? t.version : t.error;
    }
    if (b == model::hdl_backend::icarus) {
        auto& t = icarus();
        ok = t.ok();
        return ok ? t.version : t.error;
    }
    auto& t = ghdl();
    ok = t.ok();
    return ok ? t.version : t.error;
}

bool manager::handles(const model::cell& c) const
{
    return supported(c) != nullptr;
}

fs::path manager::dir_of(const model::cell& c) const
{
    auto lib = ws.library_of(c.id);
    return lib ? lib->dir : fs::path{};
}

job manager::job_of(const model::cell& c, const model::param_values& values) const
{
    job j;
    j.impl = std::get<model::hdl_impl>(c.impl);
    j.dir = dir_of(c);
    j.values = values;
    j.name = c.name;
    return j;
}

bool manager::describe(const model::cell& c, const fs::path& dir, std::vector<model::port>& ports, std::vector<model::param_def>& params,
    std::string& error) const
{
    auto j = job_of(c, {});
    j.dir = dir;
    // the defaults of the sources, not the ones found last time
    j.impl.params.clear();
    switch (j.impl.backend) {
    case model::hdl_backend::verilator: return verilator_describe(verilator(), j, ports, params, error);
    case model::hdl_backend::icarus: return icarus_describe(icarus(), j, cache_root, ports, params, error);
    case model::hdl_backend::ghdl: return ghdl_describe(ghdl(), j, cache_root, ports, params, error);
    }
    return false;
}

bool manager::ports_for(const model::cell& c, const fs::path& dir, const model::param_values& values, std::vector<model::port>& ports,
    std::string& error) const
{
    auto j = job_of(c, values);
    j.dir = dir;
    std::vector<model::param_def> params;
    switch (j.impl.backend) {
    case model::hdl_backend::verilator: return verilator_describe(verilator(), j, ports, params, error);
    case model::hdl_backend::icarus: return icarus_describe(icarus(), j, cache_root, ports, params, error);
    case model::hdl_backend::ghdl: return ghdl_describe(ghdl(), j, cache_root, ports, params, error);
    }
    return false;
}

bool manager::is_built(const model::cell& c, const model::param_values& values) const
{
    if (!supported(c)) return false;
    auto j = job_of(c, values);
    bool ok;
    tool_status(j.impl.backend, ok);
    if (!ok) return false;
    auto path = build_path(j);
    std::error_code ec;
    return j.impl.backend == model::hdl_backend::ghdl ? ghdl_is_built(path) : fs::is_regular_file(path, ec);
}

void manager::build(const model::cell& c, const model::param_values& values)
{
    if (!supported(c)) return;
    queued_job j{c.id, job_of(c, values)};
    {
        std::lock_guard lk(m);
        auto& s = statuses[c.id];
        s = {};
        s.state = build_status::queued;
        queue.push_back(std::move(j));
    }
    cv.notify_all();
}

std::vector<manager::request> manager::missing_builds(const model::cell& top) const
{
    std::vector<request> r;
    std::set<std::string> seen;
    std::set<model::id> visited;

    auto walk = [&](auto& self, const model::cell& c, int depth) -> void {
        auto sch = c.get_schematic();
        if (!sch || depth > 64 || !visited.insert(c.id).second) return;
        for (auto& i : sch->instances) {
            auto sub = ws.find_cell(i.cell);
            if (!sub) continue;
            if (supported(*sub)) {
                auto values = model::resolve_params(*sub->external_params(), i.params);
                auto key = std::to_string(sub->id) + "\n" + model::params_key(values);
                if (seen.insert(key).second && !is_built(*sub, values)) r.push_back({sub->id, values});
            } else {
                self(self, *sub, depth + 1);
            }
        }
    };
    walk(walk, top, 0);
    return r;
}

build_status manager::status(model::id cell) const
{
    std::lock_guard lk(m);
    auto it = statuses.find(cell);
    return it == statuses.end() ? build_status{} : it->second;
}

bool manager::busy() const
{
    std::lock_guard lk(m);
    return working || !queue.empty();
}

bool manager::run(const queued_job& j, fs::path& result) const
{
    {
        std::lock_guard lk(m);
        auto& s = statuses[j.cell];
        s = {};
        s.state = build_status::building;
    }

    auto on_line = [&](const std::string& line) {
        std::lock_guard lk(m);
        auto& lines = statuses[j.cell].log;
        lines.push_back(line);
        if (lines.size() > max_log_lines) lines.erase(lines.begin(), lines.begin() + (lines.size() - max_log_lines));
    };

    std::string error;
    std::vector<diagnostic> diagnostics;
    bool ok = false;
    switch (j.v.impl.backend) {
    case model::hdl_backend::verilator: ok = verilator_build(verilator(), j.v, cache_root, result, error, &diagnostics, on_line); break;
    case model::hdl_backend::icarus: ok = icarus_build(icarus(), j.v, cache_root, result, error, &diagnostics, on_line); break;
    case model::hdl_backend::ghdl: ok = ghdl_build(ghdl(), j.v, cache_root, result, error, &diagnostics, on_line); break;
    }

    std::lock_guard lk(m);
    auto& s = statuses[j.cell];
    s.state = ok ? build_status::done : build_status::failed;
    s.error = error;
    s.diagnostics = std::move(diagnostics);
    log->add(ok ? "built " + j.v.name : "building " + j.v.name + " failed: " + error);
    return ok;
}

void manager::loop()
{
    std::unique_lock lk(m);
    while (true) {
        cv.wait(lk, [&] { return quit || !queue.empty(); });
        if (quit) break;
        auto j = std::move(queue.front());
        queue.pop_front();
        working = true;
        lk.unlock();

        fs::path result;
        run(j, result);

        lk.lock();
        working = false;
    }
}

std::unique_ptr<sim::component> manager::create(const model::cell& c, const model::param_values& values, std::string& error) const
{
    if (!supported(c)) return nullptr;

    auto j = job_of(c, values);
    auto backend = j.impl.backend;
    bool tools_ok;
    auto status_text = tool_status(backend, tools_ok);
    if (!tools_ok) {
        error = status_text;
        return nullptr;
    }

    auto path = build_path(j);
    if (!is_built(c, values) && !run({c.id, j}, path)) {
        error = "building '" + c.name + "' failed: " + status(c.id).error;
        return nullptr;
    }

    if (backend == model::hdl_backend::ghdl) {
        fs::path bridge;
        if (!ghdl_bridge(ghdl(), cache_root, bridge, error)) return nullptr;
        // the ports for these generics
        std::vector<model::port> ports = c.ports;
        if (values != model::resolve_params(*c.external_params(), {}) && !ports_for(c, dir_of(c), values, ports, error)) return nullptr;
        return cosim::component::start(c.name, ports, ghdl_launch(ghdl(), j, path, bridge), log, error);
    }

    if (backend == model::hdl_backend::verilator) {
        auto p = plugins.load(path.string(), {}, error);
        if (!p) return nullptr;
        auto d = p->find(j.impl.top);
        if (!d) {
            error = "the build of '" + c.name + "' has no model";
            return nullptr;
        }
        return plugins.create_from(p, *d, c, values, dir_of(c), error);
    }

    icarus_design design;
    fs::path bridge;
    if (!icarus_load(path, design) || !icarus_bridge(icarus(), cache_root, bridge, error)) {
        if (error.empty()) error = "the build of '" + c.name + "' is missing";
        return nullptr;
    }
    // the ports of the instance: the cell's own for the defaults
    if (values == model::resolve_params(*c.external_params(), {})) {
        bool same = design.ports.size() == c.ports.size();
        for (size_t k = 0; same && k < c.ports.size(); k++)
            same = design.ports[k].name == c.ports[k].name && design.ports[k].dir == c.ports[k].dir && design.ports[k].width == c.ports[k].width;
        if (!same) {
            error = "the ports of '" + c.name + "' changed in the sources, refresh its interface";
            return nullptr;
        }
    }
    return cosim::component::start(c.name, design.ports, icarus_launch(icarus(), path, bridge, dir_of(c)), log, error);
}

}
