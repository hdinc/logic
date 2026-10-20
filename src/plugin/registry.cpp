#include "registry.h"
#include "../io/firmware.h"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <set>

namespace logic::plugin {

namespace fs = std::filesystem;

void message_log::add(std::string s)
{
    std::lock_guard lk(m);
    messages.push_back(std::move(s));
}

std::vector<std::string> message_log::take()
{
    std::lock_guard lk(m);
    return std::move(messages);
}

loaded_plugin::~loaded_plugin()
{
    lib.reset();
    std::error_code ec;
    if (!copy.empty()) fs::remove(copy, ec);
}

const logic_model_desc* loaded_plugin::find(const std::string& model) const
{
    for (uint32_t k = 0; k < info->model_count; k++)
        if (info->models[k].name && model == info->models[k].name) return &info->models[k];
    return nullptr;
}

namespace {

// parameter values in the form of the abi, with the strings they point to
class param_storage {
public:
    param_storage(const logic_model_desc& d, const model::param_values& values)
    {
        // every declared parameter gets a value, the default if the given
        // one is missing or of another type
        strings.resize(d.param_count);
        for (uint32_t k = 0; k < d.param_count; k++) {
            auto& p = d.params[k];
            logic_param_value v{p.name, p.def_int, nullptr};
            auto it = values.find(p.name);
            if (p.type == LOGIC_PARAM_INT || p.type == LOGIC_PARAM_BOOL) {
                if (it != values.end()) {
                    if (auto i = std::get_if<int64_t>(&it->second)) v.i = *i;
                    if (auto b = std::get_if<bool>(&it->second)) v.i = *b;
                }
            } else {
                strings[k] = p.def_text ? p.def_text : "";
                if (it != values.end())
                    if (auto s = std::get_if<std::string>(&it->second)) strings[k] = *s;
                v.s = strings[k].c_str();
            }
            list.push_back(v);
        }
    }

    logic_params get() const { return {list.data(), (uint32_t)list.size()}; }

private:
    std::vector<std::string> strings;
    std::vector<logic_param_value> list;
};

bool collect_ports(const logic_model_desc& d, const model::param_values& values, std::vector<model::port>& ports, std::string& error)
{
    struct sink_data {
        std::vector<model::port>* ports;
        std::string* error;
    } data{&ports, &error};

    logic_port_sink sink;
    sink.data = &data;
    sink.add = [](void* p, const logic_port_desc* d) {
        auto s = (sink_data*)p;
        model::port r;
        r.name = d->name ? d->name : "";
        r.dir = d->dir == LOGIC_OUTPUT ? model::direction::output : d->dir == LOGIC_INOUT ? model::direction::inout : model::direction::input;
        r.width = (int)d->width;
        r.description = d->description ? d->description : "";
        s->ports->push_back(std::move(r));
    };
    sink.error = [](void* p, const char* message) {
        auto s = (sink_data*)p;
        *s->error = message ? message : "invalid parameters";
    };

    ports.clear();
    error.clear();
    param_storage params(d, values);
    auto lp = params.get();
    if (!d.get_ports(&lp, &sink)) {
        if (error.empty()) error = "the model doesn't work with these parameters";
        return false;
    }

    std::set<std::string> names;
    for (auto& p : ports) {
        if (p.name.empty()) error = "the model has a port without a name";
        else if (!names.insert(p.name).second) error = "the model has two ports named '" + p.name + "'";
        else if (p.width < 1 || p.width > model::max_width) error = "port '" + p.name + "' of the model is " + std::to_string(p.width) + " bits wide";
        if (!error.empty()) return false;
    }
    return true;
}

// the optional field of a model is there
#define HAS_FIELD(d, field) ((d).struct_size >= offsetof(logic_model_desc, field) + sizeof((d).field) && (d).field)

// the debug table of a model as the simulator's debug interface. keeps the
// breakpoints it set.
class debug_adapter : public sim::debug_interface {
public:
    debug_adapter(const logic_debug_vtable* t, void** inst)
        : t(t)
        , inst(inst)
    {
    }

    bool has(size_t offset) const { return t->struct_size >= offset + sizeof(void*); }
#define DBG(f) (has(offsetof(logic_debug_vtable, f)) && t->f)

    std::vector<sim::memory_space> memories() const override
    {
        std::vector<sim::memory_space> r;
        if (!DBG(memory_count) || !DBG(memory_info)) return r;
        uint32_t n = t->memory_count(*inst);
        for (uint32_t k = 0; k < n; k++) {
            logic_memory_info m{};
            if (t->memory_info(*inst, k, &m)) r.push_back({m.name ? m.name : "memory", m.size});
        }
        return r;
    }

    bool read_memory(int space, uint64_t address, std::span<uint8_t> out) const override
    {
        return DBG(read_memory) && t->read_memory(*inst, (uint32_t)space, address, out.data(), out.size());
    }

    bool write_memory(int space, uint64_t address, std::span<const uint8_t> data) override
    {
        return DBG(write_memory) && t->write_memory(*inst, (uint32_t)space, address, data.data(), data.size());
    }

    std::optional<uint64_t> pc() const override
    {
        uint64_t v;
        if (DBG(pc) && t->pc(*inst, &v)) return v;
        return std::nullopt;
    }

    int disassemble(uint64_t address, std::string& text) const override
    {
        if (!DBG(disassemble)) return 0;
        char buf[256] = {};
        auto n = t->disassemble(*inst, address, buf, sizeof(buf));
        text = buf;
        return (int)n;
    }

    void set_breakpoint(uint64_t address, bool on) override
    {
        if (!DBG(set_breakpoint)) return;
        t->set_breakpoint(*inst, address, on ? 1 : 0);
        std::erase(bps, address);
        if (on) bps.push_back(address);
    }

    std::vector<uint64_t> breakpoints() const override { return bps; }

    void step_instruction() override
    {
        if (DBG(step_instruction)) t->step_instruction(*inst);
    }
#undef DBG

    // something to debug at all
    bool useful() const { return !memories().empty() || pc().has_value(); }

private:
    const logic_debug_vtable* t;
    void** inst;
    std::vector<uint64_t> bps;
};

// the simulation model of one instance, calls into the plugin
class c_model_component : public sim::component {
public:
    c_model_component(std::shared_ptr<loaded_plugin> p, const logic_model_desc* d, std::shared_ptr<message_log> log, std::string dir)
        : plugin(std::move(p))
        , d(d)
        , log(std::move(log))
        , dir(std::move(dir))
    {
        host.data = this;
        host.log = [](void* self, logic_log_level level, const char* message) {
            auto c = (c_model_component*)self;
            const char* prefix = level == LOGIC_LOG_ERROR ? "error" : level == LOGIC_LOG_WARNING ? "warning" : "info";
            c->log->add(std::string(c->d->name) + " (" + prefix + "): " + (message ? message : ""));
        };
        host.library_dir = this->dir.c_str();
    }

    // false if the model refused, it logged why
    bool create(const logic_params& params)
    {
        inst = d->create(&params, &host);
        if (inst && HAS_FIELD(*d, debug)) {
            dbg = std::make_unique<debug_adapter>(d->debug, &inst);
            // models without memories or a program counter have nothing to show
            if (!dbg->useful()) dbg.reset();
        }
        return inst != nullptr;
    }

    // reads the files of the file parameters and gives them to the model
    bool load_files(const model::param_values& values, const fs::path& dir, std::string& error)
    {
        for (uint32_t k = 0; k < d->param_count; k++) {
            auto& p = d->params[k];
            if (p.type != LOGIC_PARAM_FILE) continue;
            auto it = values.find(p.name);
            auto file = it == values.end() ? std::string(p.def_text ? p.def_text : "") : std::get<std::string>(it->second);
            if (file.empty()) continue;

            fs::path path = file;
            if (path.is_relative()) path = dir / path;
            io::firmware fw;
            if (!io::load_firmware(path, fw, error)) return false;
            if (!HAS_FIELD(*d, load_image)) {
                error = std::string("model '") + d->name + "' doesn't take files";
                return false;
            }
            std::vector<logic_segment> segments;
            for (auto& s : fw.segments)
                segments.push_back({s.address, s.bytes.data(), s.bytes.size()});
            logic_image image{segments.data(), (uint32_t)segments.size(), fw.has_entry ? 1 : 0, fw.entry};
            if (!d->load_image(inst, p.name, &image, &host)) {
                error = std::string("model '") + d->name + "' didn't take " + path.filename().string();
                return false;
            }
        }
        return true;
    }

    sim::debug_interface* debug() override { return dbg.get(); }

    bool interact(const std::string& action, int64_t value) override
    {
        return HAS_FIELD(*d, interact) && d->interact(inst, action.c_str(), value) != 0;
    }

    ~c_model_component() override
    {
        if (inst) d->destroy(inst);
    }

    void eval(std::span<sim::value> ports, sim::context& ctx) override
    {
        values.resize(ports.size());
        for (size_t p = 0; p < ports.size(); p++) {
            auto data = ports[p].data();
            auto n = ports[p].words();
            values[p] = {data, data + n, data + 2 * n, (uint32_t)ports[p].width()};
        }

        logic_context c{ctx.now(), ctx.woken() ? 1 : 0, ctx.wake(), nullptr};
        d->eval(inst, values.data(), &c);
        ctx.wake_at(c.wake);
        if (c.pause) ctx.request_pause(c.pause);

        for (auto& v : ports)
            v.normalize();
    }

    void reset() override { d->reset(inst); }

    void inspect(std::vector<sim::inspect_item>& out) const override
    {
        if (!d->inspect) return;
        logic_inspect_sink sink;
        sink.data = &out;
        sink.value = [](void* p, const char* name, const logic_value* v) {
            sim::value r((int)v->width, 0);
            auto data = r.data();
            int n = r.words();
            for (int w = 0; w < n; w++) {
                data[w] = v->bits[w];
                data[n + w] = v->x[w];
                data[2 * n + w] = v->z[w];
            }
            r.normalize();
            ((std::vector<sim::inspect_item>*)p)->push_back({name ? name : "", std::move(r), {}});
        };
        d->inspect(inst, &sink);
    }

    bool save_state(std::vector<uint8_t>& out) const override
    {
        if (!d->save_state) return false;
        out.clear();
        logic_state_writer w;
        w.data = &out;
        w.write = [](void* p, const void* bytes, size_t size) {
            auto v = (std::vector<uint8_t>*)p;
            v->insert(v->end(), (const uint8_t*)bytes, (const uint8_t*)bytes + size);
        };
        return d->save_state(inst, &w) != 0;
    }

    bool load_state(std::span<const uint8_t> in) override { return d->load_state && d->load_state(inst, in.data(), in.size()) != 0; }

private:
    std::shared_ptr<loaded_plugin> plugin;
    const logic_model_desc* d;
    std::shared_ptr<message_log> log;
    std::string dir;
    logic_host host{};
    void* inst = nullptr;
    std::vector<logic_value> values;
    std::unique_ptr<debug_adapter> dbg;
};

bool has_extension(const std::string& s)
{
    auto ext = fs::path(s).extension().string();
    return ext == ".so" || ext == ".dll" || ext == ".dylib";
}

}

namespace {

fs::path copies_dir()
{
    std::error_code ec;
    return fs::temp_directory_path(ec) / "logic-plugins";
}

}

registry::registry(const model::workspace& w)
    : ws(w)
    , log(std::make_shared<message_log>())
{
    // copies left behind by a crash. copies in use by another running
    // program can't be removed on windows and don't matter on linux, they
    // are already loaded.
    std::error_code ec;
    for (auto it = fs::directory_iterator(copies_dir(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ignored;
        fs::remove(it->path(), ignored);
    }
}

void registry::add_default_dirs(const fs::path& root)
{
    search_dirs.push_back(root / "plugins");
    if (auto exe = executable_dir(); !exe.empty()) search_dirs.push_back(exe / "plugins");

    if (auto env = std::getenv("LOGIC_PLUGIN_PATH")) {
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        std::string s = env;
        size_t start = 0;
        while (start <= s.size()) {
            auto end = s.find(sep, start);
            if (end == std::string::npos) end = s.size();
            if (end > start) search_dirs.push_back(s.substr(start, end - start));
            start = end + 1;
        }
    }
}

fs::path registry::resolve(const std::string& plugin, const fs::path& library_dir) const
{
    if (plugin.empty()) return {};
    std::error_code ec;

    if (plugin.find('/') != std::string::npos || plugin.find('\\') != std::string::npos || has_extension(plugin)) {
        auto p = fs::path(plugin);
        if (p.is_relative()) p = library_dir / p;
        return fs::is_regular_file(p, ec) ? p : fs::path{};
    }

    for (auto& d : search_dirs) {
        for (auto name : {plugin + library_extension(), "lib" + plugin + library_extension()}) {
            auto p = d / name;
            if (fs::is_regular_file(p, ec)) return p;
        }
    }
    return {};
}

std::shared_ptr<loaded_plugin> registry::load(const std::string& plugin, const fs::path& library_dir, std::string& error) const
{
    auto path = resolve(plugin, library_dir);
    if (path.empty()) {
        error = plugin.empty() ? "no plugin is set" : "plugin '" + plugin + "' not found";
        return nullptr;
    }

    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);

    std::lock_guard lk(m);
    auto& slot = loaded[path];
    // the loaded one while the file stays the same
    if (slot && slot->mtime == mtime) return slot;

    auto p = std::make_shared<loaded_plugin>();
    p->path = path;
    p->mtime = mtime;

    // a private copy: the file can be rebuilt (windows locks loaded dlls)
    // and the new version gets loaded even where the loader caches by path
    auto dir = copies_dir();
    fs::create_directories(dir, ec);
    static std::mt19937_64 rng{std::random_device{}()};
    p->copy = dir / (std::to_string(rng()) + "-" + std::to_string(copies++) + "-" + path.filename().string());
    if (!fs::copy_file(path, p->copy, fs::copy_options::overwrite_existing, ec)) {
        error = "can't copy " + path.string() + " to " + p->copy.string() + ": " + ec.message();
        p->copy.clear();
        return nullptr;
    }

    p->lib = dynlib::open(p->copy, error);
    if (!p->lib) return nullptr;

    auto entry = (logic_plugin_entry_fn)p->lib->symbol(LOGIC_PLUGIN_ENTRY_NAME);
    if (!entry) {
        error = path.filename().string() + " is not a logic plugin, it doesn't export " LOGIC_PLUGIN_ENTRY_NAME;
        return nullptr;
    }
    p->info = entry(LOGIC_MODEL_ABI_VERSION);
    if (!p->info) {
        error = path.filename().string() + " doesn't support abi version " + std::to_string(LOGIC_MODEL_ABI_VERSION);
        return nullptr;
    }
    if (p->info->abi_version != LOGIC_MODEL_ABI_VERSION) {
        error = path.filename().string() + " was built for abi version " + std::to_string(p->info->abi_version) + ", this program has "
            + std::to_string(LOGIC_MODEL_ABI_VERSION);
        return nullptr;
    }
    for (uint32_t k = 0; k < p->info->model_count; k++) {
        auto& d = p->info->models[k];
        // everything up to load_state is required to be known
        if (d.struct_size < LOGIC_MODEL_DESC_MIN_SIZE || !d.name || !d.get_ports || !d.create || !d.destroy || !d.reset || !d.eval) {
            error = path.filename().string() + ": model " + std::to_string(k) + " is incomplete";
            return nullptr;
        }
    }

    slot = p;
    return p;
}

std::vector<registry::available> registry::scan() const
{
    std::vector<available> r;
    std::set<std::string> names;
    std::error_code ec;
    for (auto& d : search_dirs) {
        for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            auto& p = it->path();
            if (p.extension() != library_extension() || !it->is_regular_file(ec)) continue;
            auto name = p.stem().string();
            // found as lib<name> too, the name is enough
            if (names.insert(name).second) r.push_back({name, p});
        }
    }
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.name < b.name; });
    return r;
}

std::vector<model::param_def> registry::params_of(const logic_model_desc& d)
{
    std::vector<model::param_def> r;
    for (uint32_t k = 0; k < d.param_count; k++) {
        auto& p = d.params[k];
        std::string name = p.name ? p.name : "";
        std::string label = p.label ? p.label : name;
        std::string desc = p.description ? p.description : "";
        std::string text = p.def_text ? p.def_text : "";
        switch (p.type) {
        case LOGIC_PARAM_INT: r.push_back(model::int_param(name, label, p.def_int, p.min, p.max, desc)); break;
        case LOGIC_PARAM_BOOL: r.push_back(model::bool_param(name, label, p.def_int != 0, desc)); break;
        case LOGIC_PARAM_CHOICE: {
            std::vector<std::string> choices;
            for (uint32_t c = 0; c < p.choice_count; c++)
                choices.push_back(p.choices[c] ? p.choices[c] : "");
            r.push_back(model::choice_param(name, label, choices, text, desc));
            break;
        }
        case LOGIC_PARAM_TEXT: r.push_back(model::text_param(name, label, text, desc)); break;
        case LOGIC_PARAM_FILE: {
            auto d = model::file_param(name, label, desc);
            d.def = text;
            r.push_back(std::move(d));
            break;
        }
        }
    }
    return r;
}

bool registry::handles(const model::cell& c) const
{
    return std::holds_alternative<model::c_model_impl>(c.impl);
}

fs::path registry::dir_of(const model::cell& c) const
{
    auto lib = ws.library_of(c.id);
    return lib ? lib->dir : fs::path{};
}

const logic_model_desc* registry::model_of(const model::cell& c, const fs::path& dir, std::shared_ptr<loaded_plugin>& p, std::string& error) const
{
    auto impl = std::get_if<model::c_model_impl>(&c.impl);
    if (!impl) return nullptr;
    p = load(impl->plugin, dir, error);
    if (!p) return nullptr;
    auto d = p->find(impl->model);
    if (!d) error = impl->model.empty() ? "no model is set" : "plugin '" + impl->plugin + "' has no model '" + impl->model + "'";
    return d;
}

bool registry::describe(const model::cell& c, const fs::path& dir, std::vector<model::port>& ports, std::vector<model::param_def>& params,
    std::string& error) const
{
    std::shared_ptr<loaded_plugin> p;
    auto d = model_of(c, dir, p, error);
    if (!d) return false;
    params = params_of(*d);
    return collect_ports(*d, model::resolve_params(params, {}), ports, error);
}

bool registry::ports_for(const model::cell& c, const fs::path& dir, const model::param_values& values, std::vector<model::port>& ports,
    std::string& error) const
{
    std::shared_ptr<loaded_plugin> p;
    auto d = model_of(c, dir, p, error);
    return d && collect_ports(*d, values, ports, error);
}

std::unique_ptr<sim::component> registry::create(const sim::create_info& info, std::string& error) const
{
    auto& c = info.cell;
    if (!handles(c)) return nullptr;

    // the plugin is found from the cell's library, files from the instance's
    std::shared_ptr<loaded_plugin> p;
    auto d = model_of(c, dir_of(c), p, error);
    if (!d) return nullptr;
    return create_from(p, *d, c, info.params, info.dir.empty() ? dir_of(c) : info.dir, error);
}

std::unique_ptr<sim::component> registry::create_from(const std::shared_ptr<loaded_plugin>& p, const logic_model_desc& model, const model::cell& c,
    const model::param_values& values, const fs::path& dir, std::string& error) const
{
    auto d = &model;
    // the ports must be the ones of the instance: the cell's own for the
    // default parameters, otherwise they came from the plugin anyway
    std::vector<model::port> ports;
    if (!collect_ports(*d, values, ports, error)) return nullptr;
    auto defs = c.external_params();
    if (defs && values == model::resolve_params(*defs, {})) {
        bool same = ports.size() == c.ports.size();
        for (size_t k = 0; same && k < ports.size(); k++)
            same = ports[k].name == c.ports[k].name && ports[k].dir == c.ports[k].dir && ports[k].width == c.ports[k].width;
        if (!same) {
            error = "the ports of '" + c.name + "' changed in the plugin, refresh its interface";
            return nullptr;
        }
    }

    auto comp = std::make_unique<c_model_component>(p, d, log, dir.string());
    param_storage params(*d, values);
    auto lp = params.get();
    if (!comp->create(lp)) {
        error = "model '" + std::string(d->name) + "' refused to start, see the messages";
        return nullptr;
    }
    if (!comp->load_files(values, dir, error)) return nullptr;
    return comp;
}

}
