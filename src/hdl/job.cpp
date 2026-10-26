#include "job.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace logic::hdl::detail {

namespace fs = std::filesystem;

std::string first_line(const std::string& s)
{
    auto line = s.substr(0, s.find('\n'));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

bool write_file(const fs::path& p, const std::string& text)
{
    std::ofstream f(p, std::ios::binary);
    f << text;
    return (bool)f;
}

std::vector<std::string> split_args(const std::string& s)
{
    std::vector<std::string> r;
    std::string cur;
    bool quoted = false, any = false;
    for (char c : s) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (!quoted && (c == ' ' || c == '\t' || c == '\n')) {
            if (any) r.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += c;
            any = true;
        }
    }
    if (any) r.push_back(cur);
    return r;
}

fs::path absolute_in(const fs::path& dir, const std::string& p)
{
    fs::path x = p;
    return x.is_relative() ? dir / x : x;
}

fs::path temp_folder(const fs::path& parent, const std::string& prefix)
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::error_code ec;
    fs::create_directories(parent, ec);
    for (;;) {
        auto p = parent / (prefix + std::to_string(rng()));
        if (fs::create_directory(p, ec)) return p;
        if (ec) return {};
    }
}

std::string hash(const std::string& s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (char ch : s) {
        h ^= (unsigned char)ch;
        h *= 0x100000001b3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

std::vector<std::pair<std::string, model::param_value>> changed_params(const job& j)
{
    std::vector<std::pair<std::string, model::param_value>> r;
    auto defaults = model::resolve_params(j.impl.params, {});
    for (auto& [name, v] : model::resolve_params(j.impl.params, j.values))
        if (!defaults.count(name) || defaults.at(name) != v) r.push_back({name, v});
    return r;
}

std::string source_fingerprint(const job& j)
{
    std::string s;
    for (auto& src : j.impl.sources)
        s += read_file(absolute_in(j.dir, src)) + "\n";
    for (auto& inc : j.impl.include_dirs) {
        std::error_code ec;
        std::vector<std::string> entries;
        for (auto it = fs::directory_iterator(absolute_in(j.dir, inc), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            auto size = it->file_size(e2);
            // libc++ counts in __int128
            auto time = (long long)it->last_write_time(e2).time_since_epoch().count();
            entries.push_back(it->path().filename().string() + " " + std::to_string(size) + " " + std::to_string(time));
        }
        std::sort(entries.begin(), entries.end());
        for (auto& e : entries)
            s += e + "\n";
    }
    return s;
}

remove_on_exit::~remove_on_exit()
{
    std::error_code ec;
    if (!p.empty()) fs::remove_all(p, ec);
}

}
