#include "manager.h"

#include <set>

namespace logic::hdl {

namespace fs = std::filesystem;

namespace {

// the last lines of a build are kept
constexpr size_t max_log_lines = 2000;

bool is_verilator_cell(const model::cell& c)
{
    auto h = std::get_if<model::hdl_impl>(&c.impl);
    return h && h->backend == model::hdl_backend::verilator;
}

}

manager::manager(const model::workspace& w, const plugin::registry& p, fs::path root)
    : ws(w)
    , plugins(p)
    , cache_root(std::move(root))
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
    std::call_once(tools_once, [this] { tools = find_verilator(); });
    return tools;
}

bool manager::handles(const model::cell& c) const
{
    return is_verilator_cell(c);
}

fs::path manager::dir_of(const model::cell& c) const
{
    auto lib = ws.library_of(c.id);
    return lib ? lib->dir : fs::path{};
}

verilator_job manager::job_of(const model::cell& c, const model::param_values& values) const
{
    verilator_job j;
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
    return verilator_describe(verilator(), j, ports, params, error);
}

bool manager::ports_for(const model::cell& c, const fs::path& dir, const model::param_values& values, std::vector<model::port>& ports,
    std::string& error) const
{
    auto j = job_of(c, values);
    j.dir = dir;
    std::vector<model::param_def> params;
    return verilator_describe(verilator(), j, ports, params, error);
}

bool manager::is_built(const model::cell& c, const model::param_values& values) const
{
    auto& t = verilator();
    if (!t.ok() || !is_verilator_cell(c)) return false;
    std::error_code ec;
    return fs::is_regular_file(verilator_plugin_path(cache_root, verilator_build_key(t, job_of(c, values))), ec);
}

void manager::build(const model::cell& c, const model::param_values& values)
{
    if (!is_verilator_cell(c)) return;
    job j{c.id, job_of(c, values)};
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
            if (is_verilator_cell(*sub)) {
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

bool manager::run(const job& j, fs::path& plugin) const
{
    {
        std::lock_guard lk(m);
        auto& s = statuses[j.cell];
        s = {};
        s.state = build_status::building;
    }

    auto on_line = [&](const std::string& line) {
        std::lock_guard lk(m);
        auto& log = statuses[j.cell].log;
        log.push_back(line);
        if (log.size() > max_log_lines) log.erase(log.begin(), log.begin() + (log.size() - max_log_lines));
    };

    std::string error;
    std::vector<diagnostic> diagnostics;
    bool ok = verilator_build(verilator(), j.v, cache_root, plugin, error, &diagnostics, on_line);

    std::lock_guard lk(m);
    auto& s = statuses[j.cell];
    s.state = ok ? build_status::done : build_status::failed;
    s.error = error;
    s.diagnostics = std::move(diagnostics);
    messages.add(ok ? "built " + j.v.name : "building " + j.v.name + " failed: " + error);
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

        fs::path plugin;
        run(j, plugin);

        lk.lock();
        working = false;
    }
}

std::unique_ptr<sim::component> manager::create(const model::cell& c, const model::param_values& values, std::string& error) const
{
    if (!is_verilator_cell(c)) return nullptr;

    auto& t = verilator();
    if (!t.ok()) {
        error = t.error;
        return nullptr;
    }
    auto j = job_of(c, values);
    auto path = verilator_plugin_path(cache_root, verilator_build_key(t, j));
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) && !run({c.id, j}, path)) {
        error = "building '" + c.name + "' failed: " + status(c.id).error;
        return nullptr;
    }

    auto p = plugins.load(path.string(), {}, error);
    if (!p) return nullptr;
    auto d = p->find(std::get<model::hdl_impl>(c.impl).top);
    if (!d) {
        error = "the build of '" + c.name + "' has no model";
        return nullptr;
    }
    return plugins.create_from(p, *d, c, values, dir_of(c), error);
}

}
