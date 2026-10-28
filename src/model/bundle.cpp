#include "bundle.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace logic::model {

const bundle_field* bundle_type::find_field(const std::string& n) const
{
    for (auto& f : fields)
        if (f.name == n) return &f;
    return nullptr;
}

const bundle_node* bundle_layout::find(const std::string& path) const
{
    for (auto& n : nodes)
        if (n.path == path) return &n;
    return nullptr;
}

direction flip(direction d)
{
    switch (d) {
    case direction::input: return direction::output;
    case direction::output: return direction::input;
    case direction::inout: return direction::inout;
    }
    return d;
}

std::string view_name(const std::string& bundle, bool flipped)
{
    return flipped ? "~" + bundle : bundle;
}

std::string bundle_ref(id bundle)
{
    if (bundle == no_id) return {};
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)bundle);
    return buf;
}

id parse_bundle_ref(const std::string& s)
{
    if (s.empty() || s.size() > 16) return no_id;
    id r = 0;
    for (char ch : s) {
        int d = std::isdigit((unsigned char)ch) ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
            : (ch >= 'A' && ch <= 'F')                                                ? ch - 'A' + 10
                                                                                      : -1;
        if (d < 0) return no_id;
        r = r << 4 | (id)d;
    }
    return r;
}

std::vector<std::string> split_fields(const std::string& s)
{
    std::vector<std::string> r;
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        auto a = start, b = end;
        while (a < b && std::isspace((unsigned char)s[a]))
            a++;
        while (b > a && std::isspace((unsigned char)s[b - 1]))
            b--;
        if (a < b) r.push_back(s.substr(a, b - a));
        start = end + 1;
    }
    return r;
}

std::string format_bundle_args(const param_values& args)
{
    std::string r;
    for (auto& [name, v] : args)
        if (auto i = std::get_if<int64_t>(&v)) r += (r.empty() ? "" : ", ") + name + "=" + std::to_string(*i);
    return r;
}

param_values parse_bundle_args(const std::string& s)
{
    param_values r;
    for (auto& part : split_fields(s)) {
        auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        auto name = part.substr(0, eq);
        auto value = part.substr(eq + 1);
        while (!name.empty() && std::isspace((unsigned char)name.back()))
            name.pop_back();
        size_t a = 0;
        while (a < value.size() && std::isspace((unsigned char)value[a]))
            a++;
        value = value.substr(a);
        if (name.empty() || value.empty()) continue;
        char* end = nullptr;
        long long v = std::strtoll(value.c_str(), &end, 10);
        if (*end != 0) continue;
        r[name] = int64_t(v);
    }
    return r;
}

std::string join_fields(const std::vector<std::string>& paths)
{
    std::string r;
    for (auto& p : paths)
        r += (r.empty() ? "" : ", ") + p;
    return r;
}

}
