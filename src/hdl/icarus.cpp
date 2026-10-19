#include "icarus.h"
#include "process.h"

#include "../cosim/cosim_protocol.h"

#include <algorithm>
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

// changes when the testbench or the compile commands change
constexpr int testbench_version = 1;

const char* const testbench_name = "logic_tb";

// iverilog arguments without output and top
std::vector<std::string> compile_args(const icarus_tools& tools, const job& j)
{
    auto& h = j.impl;
    std::vector<std::string> r{tools.iverilog.string(), h.language == model::hdl_language::verilog ? "-g2005" : "-g2012"};
    for (auto& i : h.include_dirs)
        r.push_back("-I" + absolute_in(j.dir, i).string());
    for (auto& d : h.defines)
        r.push_back("-D" + d);
    for (auto& a : split_args(h.args))
        r.push_back(a);
    return r;
}

std::string value_text(const model::param_value& v)
{
    if (auto i = std::get_if<int64_t>(&v)) return std::to_string(*i);
    if (auto b = std::get_if<bool>(&v)) return *b ? "1" : "0";
    return "\"" + std::get<std::string>(v) + "\"";
}

bool check_job(const icarus_tools& tools, const job& j, std::string& error)
{
    if (!tools.ok()) error = tools.error;
    else if (j.impl.language == model::hdl_language::vhdl) error = "icarus verilog can't simulate vhdl";
    else if (j.impl.top.empty()) error = "no top module is set";
    else if (j.impl.sources.empty()) error = "there are no source files";
    return error.empty();
}

bool run_tool(const std::vector<std::string>& args, const fs::path& dir, std::string& error, std::vector<diagnostic>* diagnostics,
    const std::function<void(const std::string&)>& on_line, const environment& env = {}, process_result* result = nullptr)
{
    auto r = run_process(args, dir, on_line, env);
    auto messages = parse_icarus_messages(r.output);
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

std::string ports_text(const std::vector<model::port>& ports)
{
    std::string s;
    for (auto& p : ports)
        s += std::string(model::to_string(p.dir)) + " " + std::to_string(p.width) + " " + p.name + "\n";
    return s;
}

}

std::vector<diagnostic> parse_icarus_messages(const std::string& output)
{
    std::vector<diagnostic> r;
    static const std::regex re(R"(^((?:[A-Za-z]:)?[^:\s][^:]*):(\d+): (?:(error|warning|sorry): )?(.*)$)");
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (!std::regex_match(line, m, re)) continue;
        diagnostic d;
        d.file = m[1];
        d.line = std::stoi(m[2]);
        d.severity = m[3] == "warning" ? "warning" : "error";
        d.message = m[3].matched && m[3] != "error" && m[3] != "warning" ? std::string(m[3]) + ": " + std::string(m[4]) : std::string(m[4]);
        r.push_back(std::move(d));
    }
    return r;
}

icarus_tools find_icarus()
{
    icarus_tools t;
    t.iverilog = find_program("iverilog");
    t.vvp = find_program("vvp");
    t.iverilog_vpi = find_program("iverilog-vpi");
    if (t.iverilog.empty() || t.vvp.empty()) {
        t.error = "icarus verilog (iverilog, vvp) not found";
        return t;
    }
    if (t.iverilog_vpi.empty()) {
        t.error = "iverilog-vpi, needed to build the bridge, isn't on PATH";
        return t;
    }
    auto r = run_process({t.iverilog.string(), "-V"});
    t.version = first_line(r.output);
    if (t.version.empty()) t.error = "can't run iverilog";
    return t;
}

bool icarus_bridge(const icarus_tools& tools, const fs::path& cache_root, fs::path& dir, std::string& error,
    const std::function<void(const std::string&)>& on_line)
{
    if (!tools.ok()) {
        error = tools.error;
        return false;
    }
    dir = cache_root / "icarus" / ("bridge-" + hash(tools.version + "\n" + cosim::bridge_source + cosim::bridge_protocol));
    std::error_code ec;
    if (fs::is_regular_file(dir / "logic_vpi.vpi", ec)) return true;

    auto tmp = temp_folder(cache_root / "icarus", "bridge.tmp-");
    if (tmp.empty()) {
        error = "can't create a folder in " + (cache_root / "icarus").string();
        return false;
    }
    remove_on_exit clean{tmp};
    if (!write_file(tmp / "logic_vpi.c", cosim::bridge_source) || !write_file(tmp / "cosim_protocol.h", cosim::bridge_protocol)) {
        error = "can't write the bridge's source";
        return false;
    }

    std::vector<std::string> args{tools.iverilog_vpi.string(), "logic_vpi.c"};
#ifdef _WIN32
    args.push_back("-lws2_32");
#endif
    if (on_line) on_line("building the vpi bridge");
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

bool icarus_describe(const icarus_tools& tools, const job& j, const fs::path& cache_root, std::vector<model::port>& ports,
    std::vector<model::param_def>& params, std::string& error, std::vector<diagnostic>* diagnostics)
{
    if (!check_job(tools, j, error)) return false;
    fs::path bridge;
    if (!icarus_bridge(tools, cache_root, bridge, error)) return false;

    auto tmp = temp_folder(fs::temp_directory_path() / "logic-icarus", "describe-");
    if (tmp.empty()) {
        error = "can't create a temporary folder";
        return false;
    }
    remove_on_exit clean{tmp};

    auto args = compile_args(tools, j);
    args.insert(args.end(), {"-s", j.impl.top, "-o", (tmp / "design.vvp").string()});
    for (auto& [name, v] : changed_params(j))
        args.push_back("-P" + j.impl.top + "." + name + "=" + value_text(v));
    for (auto& s : j.impl.sources)
        args.push_back(absolute_in(j.dir, s).string());
    if (!run_tool(args, j.dir, error, diagnostics, {})) return false;

    process_result r;
    environment env{{LOGIC_COSIM_ENV_MODE, "describe"}, {LOGIC_COSIM_ENV_TOP, j.impl.top}};
    if (!run_tool({tools.vvp.string(), "-n", "-M", bridge.string(), "-m", "logic_vpi", (tmp / "design.vvp").string()}, j.dir, error, nullptr, {}, env,
            &r)) {
        return false;
    }

    ports.clear();
    params.clear();
    bool end = false;
    std::istringstream in(r.output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream l(line);
        std::string tag;
        l >> tag;
        if (tag == "LOGIC_ERROR") {
            std::getline(l, error);
            error = error.substr(error.find_first_not_of(' '));
            return false;
        } else if (tag == "LOGIC_PORT") {
            std::string dir;
            model::port p;
            l >> dir >> p.width >> p.name;
            p.dir = dir == "out" ? model::direction::output : dir == "inout" ? model::direction::inout : model::direction::input;
            ports.push_back(std::move(p));
        } else if (tag == "LOGIC_PARAM") {
            std::string kind, name, value;
            int size = 0, is_signed = 0;
            l >> kind >> size >> is_signed >> name;
            std::getline(l, value);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            if (kind == "string") {
                params.push_back(model::text_param(name, name, value));
            } else if (kind == "real") {
                params.push_back(model::text_param(name, name, value, "real number"));
            } else if (size == 1 && !is_signed) {
                params.push_back(model::bool_param(name, name, value == "1"));
            } else {
                int64_t min = 0, max = INT64_MAX, def = 0;
                if (is_signed && size < 64) {
                    min = -(int64_t(1) << (size - 1));
                    max = (int64_t(1) << (size - 1)) - 1;
                } else if (is_signed) {
                    min = INT64_MIN;
                } else if (size < 63) {
                    max = (int64_t(1) << size) - 1;
                }
                try {
                    def = std::stoll(value);
                } catch (const std::exception&) {
                }
                params.push_back(model::int_param(name, name, std::clamp(def, min, max), min, max, std::to_string(size) + " bits"));
            }
        } else if (tag == "LOGIC_END") {
            end = true;
        }
    }
    if (!end) {
        error = "the design didn't describe itself: " + first_line(r.output);
        return false;
    }
    // the order of the declarations, not alphabetical
    return true;
}

std::string icarus_testbench(const job& j, const std::vector<model::port>& ports)
{
    auto range = [](int w) { return w > 1 ? "[" + std::to_string(w - 1) + ":0] " : std::string(); };
    std::ostringstream s;
    s << "// generated by logic, the vpi bridge drives the ports of " << j.impl.top << "\n";
    s << "module " << testbench_name << ";\n";
    for (auto& p : ports) {
        auto r = range(p.width);
        if (p.dir == model::direction::input) {
            s << "    reg " << r << "in_" << p.name << ";\n";
        } else if (p.dir == model::direction::output) {
            s << "    wire " << r << "out_" << p.name << ";\n";
        } else {
            s << "    wire " << r << "io_" << p.name << ";\n";
            s << "    reg " << r << "drv_" << p.name << " = " << p.width << "'bz;\n";
            s << "    assign (weak0, weak1) io_" << p.name << " = drv_" << p.name << ";\n";
        }
    }
    s << "    " << j.impl.top;
    auto changed = changed_params(j);
    if (!changed.empty()) {
        s << " #(";
        for (size_t k = 0; k < changed.size(); k++)
            s << (k ? ", " : "") << "." << changed[k].first << "(" << value_text(changed[k].second) << ")";
        s << ")";
    }
    s << " dut (";
    for (size_t k = 0; k < ports.size(); k++) {
        auto& p = ports[k];
        auto prefix = p.dir == model::direction::input ? "in_" : p.dir == model::direction::output ? "out_" : "io_";
        s << (k ? ", " : "") << "." << p.name << "(" << prefix << p.name << ")";
    }
    s << ");\nendmodule\n";
    return s.str();
}

std::string icarus_build_key(const icarus_tools& tools, const job& j)
{
    std::string s = "testbench " + std::to_string(testbench_version) + "\n" + tools.version + "\n" + j.impl.top + "\n";
    for (auto& a : compile_args(tools, j))
        s += a + "\n";
    for (auto& [name, v] : changed_params(j))
        s += name + "=" + value_text(v) + "\n";
    for (auto& src : j.impl.sources)
        s += absolute_in(j.dir, src).string() + "\n";
    s += source_fingerprint(j);
    return hash(s);
}

fs::path icarus_design_path(const fs::path& cache_root, const std::string& key)
{
    return cache_root / "icarus" / key / "design.vvp";
}

bool icarus_build(const icarus_tools& tools, const job& j, const fs::path& cache_root, fs::path& design, std::string& error,
    std::vector<diagnostic>* diagnostics, const std::function<void(const std::string&)>& on_line)
{
    if (!check_job(tools, j, error)) return false;
    design = icarus_design_path(cache_root, icarus_build_key(tools, j));
    std::error_code ec;
    if (fs::is_regular_file(design, ec)) return true;

    fs::path bridge;
    if (!icarus_bridge(tools, cache_root, bridge, error, on_line)) return false;
    if (on_line) on_line("compiling " + j.name + " with " + tools.version);

    std::vector<model::port> ports;
    std::vector<model::param_def> params;
    if (!icarus_describe(tools, j, cache_root, ports, params, error, diagnostics)) return false;

    auto tmp = temp_folder(cache_root / "icarus", "build.tmp-");
    if (tmp.empty()) {
        error = "can't create a folder in " + (cache_root / "icarus").string();
        return false;
    }
    remove_on_exit clean{tmp};
    if (!write_file(tmp / "testbench.v", icarus_testbench(j, ports)) || !write_file(tmp / "ports.txt", ports_text(ports))) {
        error = "can't write the testbench";
        return false;
    }

    auto args = compile_args(tools, j);
    args.insert(args.end(), {"-s", testbench_name, "-o", (tmp / "design.vvp").string(), (tmp / "testbench.v").string()});
    for (auto& s : j.impl.sources)
        args.push_back(absolute_in(j.dir, s).string());
    if (!run_tool(args, j.dir, error, diagnostics, on_line)) return false;

    fs::rename(tmp, design.parent_path(), ec);
    if (ec && !fs::is_regular_file(design)) {
        error = "can't create " + design.parent_path().string() + ": " + ec.message();
        return false;
    }
    if (on_line) on_line("compiled " + design.string());
    return true;
}

bool icarus_load(const fs::path& design, icarus_design& out)
{
    std::error_code ec;
    if (!fs::is_regular_file(design, ec)) return false;
    out.vvp = design;
    out.ports.clear();
    std::istringstream in(read_file(design.parent_path() / "ports.txt"));
    std::string dir;
    model::port p;
    while (in >> dir >> p.width >> p.name) {
        p.dir = dir == "output" ? model::direction::output : dir == "inout" ? model::direction::inout : model::direction::input;
        out.ports.push_back(p);
    }
    return true;
}

cosim::launch icarus_launch(const icarus_tools& tools, const fs::path& design, const fs::path& bridge_dir, const fs::path& dir)
{
    cosim::launch l;
    // -n: $stop ends instead of waiting for commands
    l.args = {tools.vvp.string(), "-n", "-M", bridge_dir.string(), "-m", "logic_vpi", design.string()};
    l.dir = dir;
    l.testbench = testbench_name;
    return l;
}

}
