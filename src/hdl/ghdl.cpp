#include "ghdl.h"
#include "process.h"

#include "../cosim/cosim_protocol.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace logic::cosim {
extern const char* const bridge_source;
extern const char* const bridge_protocol;
}

namespace logic::hdl {

namespace fs = std::filesystem;
using namespace detail;

namespace {

// changes when the testbench or the commands change
constexpr int testbench_version = 1;

const char* const testbench_name = "logic_tb";

std::string lower(std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// the first word of a type, lower case: "std_logic_vector" of
// "std_logic_vector(7 downto 0)"
std::string type_name(const std::string& type)
{
    auto l = lower(type);
    size_t e = 0;
    while (e < l.size() && (std::isalnum((unsigned char)l[e]) || l[e] == '_' || l[e] == '.'))
        e++;
    auto n = l.substr(0, e);
    // ieee.std_logic_1164.std_logic
    auto dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(dot + 1);
}

bool is_scalar(const std::string& type)
{
    auto n = type_name(type);
    return n == "std_logic" || n == "std_ulogic" || n == "bit" || n == "boolean";
}

// options for analysis and running: the standard unless the extra arguments
// choose one, and the extra arguments
std::vector<std::string> options(const job& j)
{
    std::vector<std::string> r;
    auto extra = split_args(j.impl.args);
    bool has_std = std::any_of(extra.begin(), extra.end(), [](auto& a) { return a.rfind("--std=", 0) == 0; });
    if (!has_std) r.push_back("--std=08");
    r.insert(r.end(), extra.begin(), extra.end());
    return r;
}

bool check_job(const ghdl_tools& tools, const job& j, std::string& error)
{
    if (!tools.ok()) error = tools.error;
    else if (j.impl.language != model::hdl_language::vhdl) error = "ghdl only simulates vhdl";
    else if (j.impl.top.empty()) error = "no top entity is set";
    else if (j.impl.sources.empty()) error = "there are no source files";
    return error.empty();
}

bool run_tool(const std::vector<std::string>& args, const fs::path& dir, std::string& error, std::vector<diagnostic>* diagnostics,
    const std::function<void(const std::string&)>& on_line, const environment& env = {}, process_result* result = nullptr)
{
    auto r = run_process(args, dir, on_line, env);
    auto messages = parse_ghdl_messages(r.output);
    if (diagnostics) diagnostics->insert(diagnostics->end(), messages.begin(), messages.end());
    if (result) *result = r;
    if (!r.error.empty()) {
        error = r.error;
        return false;
    }
    if (r.ok()) return true;
    auto first = std::find_if(messages.begin(), messages.end(), [](auto& d) { return d.severity == "error"; });
    if (first != messages.end())
        error = fs::path(first->file).filename().string() + ":" + std::to_string(first->line) + ": " + first->message;
    else
        error = fs::path(args[0]).filename().string() + " failed: " + first_line(r.output);
    return false;
}

// generic values for -g: the changed ones and the ones without a default
std::vector<std::string> generic_args(const job& j, const vhdl_entity& e)
{
    std::vector<std::string> r;
    auto values = model::resolve_params(j.impl.params, j.values);
    auto changed = changed_params(j);
    for (auto& g : e.generics) {
        bool is_changed = std::any_of(changed.begin(), changed.end(), [&](auto& c) { return lower(c.first) == lower(g.name); });
        if (!is_changed && !g.def.empty()) continue;
        auto it = std::find_if(values.begin(), values.end(), [&](auto& v) { return lower(v.first) == lower(g.name); });
        if (it == values.end()) continue;
        std::string v;
        if (auto i = std::get_if<int64_t>(&it->second)) v = std::to_string(*i);
        else if (auto b = std::get_if<bool>(&it->second)) v = *b ? "true" : "false";
        else v = std::get<std::string>(it->second);
        r.push_back("-g" + g.name + "=" + v);
    }
    return r;
}

// an integer literal like 8, 1_000 or 16#FF#, false if it's something else
bool parse_integer(const std::string& s, int64_t& v)
{
    std::string t;
    for (char c : s)
        if (c != '_' && !std::isspace((unsigned char)c)) t += c;
    bool neg = !t.empty() && t[0] == '-';
    if (neg) t.erase(0, 1);
    try {
        size_t used = 0;
        auto hash1 = t.find('#');
        if (hash1 != std::string::npos) {
            auto hash2 = t.find('#', hash1 + 1);
            if (hash2 != t.size() - 1) return false;
            int base = std::stoi(t.substr(0, hash1));
            v = (int64_t)std::stoull(t.substr(hash1 + 1, hash2 - hash1 - 1), &used, base);
            if (used != hash2 - hash1 - 1) return false;
        } else {
            v = std::stoll(t, &used);
            if (used != t.size()) return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    if (neg) v = -v;
    return true;
}

}

std::vector<diagnostic> parse_ghdl_messages(const std::string& output)
{
    std::vector<diagnostic> r;
    static const std::regex re(R"(^((?:[A-Za-z]:)?[^:\s][^:]*):(\d+):(\d+):\s*(?:(error|warning|note):\s*)?(.*)$)");
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (!std::regex_match(line, m, re)) continue;
        diagnostic d;
        d.file = m[1];
        d.line = std::stoi(m[2]);
        d.column = std::stoi(m[3]);
        d.severity = m[4] == "warning" || m[4] == "note" ? "warning" : "error";
        d.message = m[5];
        r.push_back(std::move(d));
    }
    return r;
}

ghdl_tools find_ghdl()
{
    ghdl_tools t;
    t.ghdl = find_program("ghdl");
    if (t.ghdl.empty()) {
        t.error = "ghdl not found";
        return t;
    }
    auto r = run_process({t.ghdl.string(), "--version"});
    t.version = first_line(r.output);
    if (!r.ok()) {
        t.error = "can't run ghdl";
        return t;
    }

    for (auto cc : {std::getenv("CC") ? std::getenv("CC") : "", "cc", "gcc"}) {
        if (*cc && !find_program(cc).empty()) {
            t.cc = cc;
            break;
        }
    }
    if (t.cc.empty()) {
        t.error = "a c compiler (cc or gcc) is needed for the vpi bridge";
        return t;
    }

    // ghdl tells its folders, but they may be wrong when it's found through
    // a symbolic link: then they're next to the real program
    auto ask = [&](const char* option) { return fs::path(first_line(run_process({t.ghdl.string(), option}).output)); };
    std::error_code ec;
    auto prefix = fs::canonical(t.ghdl, ec).parent_path().parent_path();
    t.include_dir = ask("--vpi-include-dir");
    if (!fs::exists(t.include_dir / "vpi_user.h", ec)) t.include_dir = prefix / "include" / "ghdl";
    t.lib_dir = ask("--vpi-library-dir");
    auto has_lib = [&](const fs::path& d) {
        for (auto n : {"libghdlvpi.so", "libghdlvpi.dll", "libghdlvpi.dylib", "libghdlvpi.dll.a"})
            if (fs::exists(d / n, ec)) return true;
        return false;
    };
    if (!has_lib(t.lib_dir)) t.lib_dir = prefix / "lib";
    if (!fs::exists(t.include_dir / "vpi_user.h", ec) || !has_lib(t.lib_dir)) t.error = "ghdl's vpi header or library (libghdlvpi) not found";
    return t;
}

bool ghdl_bridge(const ghdl_tools& tools, const fs::path& cache_root, fs::path& dir, std::string& error, const std::function<void(const std::string&)>& on_line)
{
    if (!tools.ok()) {
        error = tools.error;
        return false;
    }
    dir = cache_root / "ghdl" / ("bridge-" + hash(tools.version + "\n" + tools.lib_dir.string() + "\n" + cosim::bridge_source + cosim::bridge_protocol));
    std::error_code ec;
    if (fs::is_regular_file(dir / "logic_vpi.vpi", ec)) return true;

    auto tmp = temp_folder(cache_root / "ghdl", "bridge.tmp-");
    if (tmp.empty()) {
        error = "can't create a folder in " + (cache_root / "ghdl").string();
        return false;
    }
    remove_on_exit clean{tmp};
    if (!write_file(tmp / "logic_vpi.c", cosim::bridge_source) || !write_file(tmp / "cosim_protocol.h", cosim::bridge_protocol)) {
        error = "can't write the bridge's source";
        return false;
    }

    std::vector<std::string> args{tools.cc, "-shared", "-O2", "-I" + tools.include_dir.string(), "logic_vpi.c", "-o", "logic_vpi.vpi",
        "-L" + tools.lib_dir.string(), "-lghdlvpi"};
#ifdef _WIN32
    args.push_back("-lws2_32");
#else
    args.insert(args.begin() + 1, "-fPIC");
    args.push_back("-Wl,-rpath," + tools.lib_dir.string());
#endif
    if (on_line) on_line("building the vpi bridge for ghdl");
    if (!run_tool(args, tmp, error, nullptr, on_line)) {
        error = "building the vpi bridge failed: " + error;
        return false;
    }
    fs::rename(tmp, dir, ec);
    if (ec && !fs::is_regular_file(dir / "logic_vpi.vpi")) {
        error = "can't create " + dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool ghdl_entity(const job& j, vhdl_entity& entity, std::string& error)
{
    for (auto& s : j.impl.sources) {
        auto path = absolute_in(j.dir, s);
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            error = "source " + path.string() + " not found";
            return false;
        }
        if (parse_vhdl_entity(read_file(path), j.impl.top, entity, error)) return true;
        if (!error.empty()) {
            error = path.filename().string() + ": " + error;
            return false;
        }
    }
    error = "entity '" + j.impl.top + "' not found in the sources";
    return false;
}

std::vector<model::param_def> ghdl_params(const vhdl_entity& e)
{
    std::vector<model::param_def> r;
    for (auto& g : e.generics) {
        auto t = type_name(g.type);
        int64_t v = 0;
        bool literal = parse_integer(g.def, v);
        if ((t == "integer" || t == "natural" || t == "positive") && (literal || g.def.empty())) {
            int64_t min = t == "natural" ? 0 : t == "positive" ? 1 : INT32_MIN;
            r.push_back(model::int_param(g.name, g.name, std::clamp<int64_t>(g.def.empty() ? min : v, min, INT32_MAX), min, INT32_MAX, g.type));
        } else if (t == "boolean" && (g.def.empty() || lower(g.def) == "true" || lower(g.def) == "false")) {
            r.push_back(model::bool_param(g.name, g.name, lower(g.def) == "true", g.type));
        } else if (t == "string" && g.def.size() >= 2 && g.def.front() == '"' && g.def.back() == '"') {
            r.push_back(model::text_param(g.name, g.name, g.def.substr(1, g.def.size() - 2), g.type));
        } else {
            // passed to ghdl as written
            r.push_back(model::text_param(g.name, g.name, g.def, g.type + (g.def.empty() ? "" : ", default " + g.def)));
        }
    }
    return r;
}

bool ghdl_describe(const ghdl_tools& tools, const job& j, const fs::path& cache_root, std::vector<model::port>& ports,
    std::vector<model::param_def>& params, std::string& error, std::vector<diagnostic>* diagnostics)
{
    if (!check_job(tools, j, error)) return false;
    vhdl_entity e;
    if (!ghdl_entity(j, e, error)) return false;
    fs::path bridge;
    if (!ghdl_bridge(tools, cache_root, bridge, error)) return false;

    auto tmp = temp_folder(fs::temp_directory_path() / "logic-ghdl", "describe-");
    if (tmp.empty()) {
        error = "can't create a temporary folder";
        return false;
    }
    remove_on_exit clean{tmp};

    auto opts = options(j);
    std::vector<std::string> args{tools.ghdl.string(), "-a"};
    args.insert(args.end(), opts.begin(), opts.end());
    args.push_back("--workdir=" + tmp.string());
    for (auto& s : j.impl.sources)
        args.push_back(absolute_in(j.dir, s).string());
    if (!run_tool(args, j.dir, error, diagnostics, {})) return false;

    // the widths for these generics from the elaborated design
    args = {tools.ghdl.string(), "--elab-run"};
    args.insert(args.end(), opts.begin(), opts.end());
    args.push_back("--workdir=" + tmp.string());
    args.push_back(j.impl.top);
    for (auto& g : generic_args(j, e))
        args.push_back(g);
    args.push_back("--vpi=" + (bridge / "logic_vpi.vpi").string());
    process_result r;
    environment env{{LOGIC_COSIM_ENV_MODE, "describe"}, {LOGIC_COSIM_ENV_TOP, lower(j.impl.top)}};
    if (!run_tool(args, j.dir, error, diagnostics, {}, env, &r)) return false;

    std::vector<std::pair<std::string, int>> widths;
    bool end = false;
    std::istringstream in(r.output);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string tag, dir, name;
        int width = 0;
        l >> tag;
        if (tag == "LOGIC_PORT" && (l >> dir >> width >> name)) widths.push_back({lower(name), width});
        else if (tag == "LOGIC_END") end = true;
        else if (tag == "LOGIC_ERROR") {
            error = line.substr(12);
            return false;
        }
    }
    if (!end) {
        error = "the design didn't describe itself: " + first_line(r.output);
        return false;
    }

    // names and modes as declared, widths from the design
    ports.clear();
    for (auto& p : e.ports) {
        auto w = std::find_if(widths.begin(), widths.end(), [&](auto& x) { return x.first == lower(p.name); });
        if (w == widths.end()) {
            error = "port '" + p.name + "' isn't in the elaborated design";
            return false;
        }
        model::port mp;
        mp.name = p.name;
        mp.width = std::max(1, w->second);
        mp.dir = p.mode == "out" || p.mode == "buffer" ? model::direction::output : p.mode == "inout" ? model::direction::inout : model::direction::input;
        mp.description = p.type;
        ports.push_back(std::move(mp));
    }
    params = ghdl_params(e);
    return true;
}

std::string ghdl_testbench(const vhdl_entity& e, std::string& error)
{
    std::ostringstream s;
    s << "-- generated by logic, the vpi bridge drives the ports of " << e.name << "\n";
    s << e.context;
    s << "library ieee;\nuse ieee.std_logic_1164.all;\n\n";
    s << "entity " << testbench_name << " is\n";
    if (!e.generics.empty()) {
        s << "    generic (\n";
        for (size_t k = 0; k < e.generics.size(); k++) {
            auto& g = e.generics[k];
            s << "        " << g.name << " : " << g.type << (g.def.empty() ? "" : " := " + g.def) << (k + 1 < e.generics.size() ? ";" : "") << "\n";
        }
        s << "    );\n";
    }
    s << "end entity;\n\narchitecture bridge of " << testbench_name << " is\n";

    for (auto& p : e.ports) {
        if (p.mode == "linkage") {
            error = "port '" + p.name + "' is a linkage port";
            return {};
        }
        if (p.mode == "in") {
            s << "    signal in_" << p.name << " : " << p.type << ";\n";
        } else if (p.mode == "out" || p.mode == "buffer") {
            s << "    signal out_" << p.name << " : " << p.type << ";\n";
        } else {
            auto t = type_name(p.type);
            if (t == "std_ulogic" || t == "std_ulogic_vector" || t == "bit" || t == "bit_vector" || t == "boolean") {
                error = "inout port '" + p.name + "' needs a resolved type like std_logic";
                return {};
            }
            s << "    signal io_" << p.name << " : " << p.type << ";\n";
            s << "    signal drv_" << p.name << " : " << p.type << ";\n";
        }
    }

    s << "begin\n    dut : entity work." << e.name << "\n";
    if (!e.generics.empty()) {
        s << "        generic map (";
        for (size_t k = 0; k < e.generics.size(); k++)
            s << (k ? ", " : "") << e.generics[k].name << " => " << e.generics[k].name;
        s << ")\n";
    }
    if (!e.ports.empty()) {
        s << "        port map (";
        for (size_t k = 0; k < e.ports.size(); k++) {
            auto& p = e.ports[k];
            auto prefix = p.mode == "in" ? "in_" : p.mode == "inout" ? "io_" : "out_";
            s << (k ? ", " : "") << p.name << " => " << prefix << p.name;
        }
        s << ")";
    }
    s << ";\n";

    // inouts are driven weakly, what's strong is the design's
    for (auto& p : e.ports) {
        if (p.mode != "inout") continue;
        auto io = "io_" + p.name, drv = "drv_" + p.name;
        if (is_scalar(p.type)) {
            s << "    " << io << " <= 'H' when " << drv << " = '1' else 'L' when " << drv << " = '0' else 'Z';\n";
        } else {
            s << "    weak_" << p.name << " : for i in " << io << "'range generate\n";
            s << "        " << io << "(i) <= 'H' when " << drv << "(i) = '1' else 'L' when " << drv << "(i) = '0' else 'Z';\n";
            s << "    end generate;\n";
        }
    }
    s << "end architecture;\n";
    return s.str();
}

std::string ghdl_build_key(const ghdl_tools& tools, const job& j)
{
    std::string s = "testbench " + std::to_string(testbench_version) + "\n" + tools.version + "\n" + j.impl.top + "\n";
    for (auto& o : options(j))
        s += o + "\n";
    for (auto& src : j.impl.sources)
        s += absolute_in(j.dir, src).string() + "\n";
    s += source_fingerprint(j);
    return hash(s);
}

fs::path ghdl_work_path(const fs::path& cache_root, const std::string& key)
{
    return cache_root / "ghdl" / key / "work";
}

bool ghdl_is_built(const fs::path& work)
{
    std::error_code ec;
    return fs::is_regular_file(work.parent_path() / "ok", ec);
}

bool ghdl_build(const ghdl_tools& tools, const job& j, const fs::path& cache_root, fs::path& work, std::string& error,
    std::vector<diagnostic>* diagnostics, const std::function<void(const std::string&)>& on_line)
{
    if (!check_job(tools, j, error)) return false;
    work = ghdl_work_path(cache_root, ghdl_build_key(tools, j));
    std::error_code ec;
    if (ghdl_is_built(work)) return true;

    fs::path bridge;
    if (!ghdl_bridge(tools, cache_root, bridge, error, on_line)) return false;
    vhdl_entity e;
    if (!ghdl_entity(j, e, error)) return false;
    auto tb = ghdl_testbench(e, error);
    if (tb.empty()) return false;

    // built in place: ghdl keeps the paths of the files in its work folder.
    // the marker at the end tells a finished build from an interrupted one.
    if (on_line) on_line("analysing " + j.name + " with " + tools.version);
    auto tmp = work.parent_path();
    fs::remove_all(tmp, ec);
    fs::create_directories(work, ec);
    remove_on_exit clean{tmp};
    if (!write_file(tmp / "testbench.vhd", tb)) {
        error = "can't write the testbench";
        return false;
    }

    auto opts = options(j);
    std::vector<std::string> args{tools.ghdl.string(), "-a"};
    args.insert(args.end(), opts.begin(), opts.end());
    args.push_back("--workdir=" + (tmp / "work").string());
    for (auto& s : j.impl.sources)
        args.push_back(absolute_in(j.dir, s).string());
    args.push_back((tmp / "testbench.vhd").string());
    if (!run_tool(args, j.dir, error, diagnostics, on_line)) return false;

    // checks the hierarchy, the design is elaborated again when it runs
    args = {tools.ghdl.string(), "-e"};
    args.insert(args.end(), opts.begin(), opts.end());
    args.push_back("--workdir=" + (tmp / "work").string());
    args.push_back(testbench_name);
    if (!run_tool(args, tmp, error, diagnostics, on_line)) return false;

    if (!write_file(tmp / "ok", "")) {
        error = "can't write into " + tmp.string();
        return false;
    }
    clean.p.clear();
    if (on_line) on_line("analysed " + work.string());
    return true;
}

cosim::launch ghdl_launch(const ghdl_tools& tools, const job& j, const fs::path& work, const fs::path& bridge_dir)
{
    cosim::launch l;
    l.args = {tools.ghdl.string(), "--elab-run"};
    for (auto& o : options(j))
        l.args.push_back(o);
    l.args.push_back("--workdir=" + work.string());
    l.args.push_back(testbench_name);
    vhdl_entity e;
    std::string error;
    if (ghdl_entity(j, e, error))
        for (auto& g : generic_args(j, e))
            l.args.push_back(g);
    l.args.push_back("--vpi=" + (bridge_dir / "logic_vpi.vpi").string());
    l.dir = j.dir;
    l.testbench = testbench_name;
    return l;
}

}
