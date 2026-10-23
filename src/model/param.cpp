#include "param.h"

#include <algorithm>

namespace logic::model {

param_def int_param(std::string name, std::string label, int64_t def, int64_t min, int64_t max, std::string description)
{
    param_def d;
    d.name = std::move(name);
    d.label = std::move(label);
    d.type = param_def::integer;
    d.def = def;
    d.min = min;
    d.max = max;
    d.description = std::move(description);
    return d;
}

param_def bool_param(std::string name, std::string label, bool def, std::string description)
{
    param_def d;
    d.name = std::move(name);
    d.label = std::move(label);
    d.type = param_def::boolean;
    d.def = def;
    d.description = std::move(description);
    return d;
}

param_def choice_param(std::string name, std::string label, std::vector<std::string> choices, std::string def, std::string description)
{
    param_def d;
    d.name = std::move(name);
    d.label = std::move(label);
    d.type = param_def::choice;
    d.choices = std::move(choices);
    d.def = std::move(def);
    d.description = std::move(description);
    return d;
}

param_def file_param(std::string name, std::string label, std::string description)
{
    auto d = text_param(std::move(name), std::move(label), "", std::move(description));
    d.file = true;
    return d;
}

param_def bundle_param(std::string name, std::string label, std::string description)
{
    auto d = text_param(std::move(name), std::move(label), "", std::move(description));
    d.role = param_def::bundle;
    return d;
}

param_def fields_param(std::string name, std::string label, std::string bundle, bool one, std::string description)
{
    auto d = text_param(std::move(name), std::move(label), "", std::move(description));
    d.role = one ? param_def::field : param_def::fields;
    d.bundle_param = std::move(bundle);
    return d;
}

param_def text_param(std::string name, std::string label, std::string def, std::string description)
{
    param_def d;
    d.name = std::move(name);
    d.label = std::move(label);
    d.type = param_def::text;
    d.def = std::move(def);
    d.description = std::move(description);
    return d;
}

namespace {

bool valid(const param_def& d, const param_value& v)
{
    switch (d.type) {
    case param_def::integer: {
        auto i = std::get_if<int64_t>(&v);
        return i && *i >= d.min && *i <= d.max;
    }
    case param_def::boolean: return std::holds_alternative<bool>(v);
    case param_def::choice: {
        auto s = std::get_if<std::string>(&v);
        return s && std::find(d.choices.begin(), d.choices.end(), *s) != d.choices.end();
    }
    case param_def::text: return std::holds_alternative<std::string>(v);
    }
    return false;
}

}

param_values resolve_params(const std::vector<param_def>& defs, const param_values& values)
{
    param_values r;
    for (auto& d : defs) {
        auto it = values.find(d.name);
        if (it != values.end() && valid(d, it->second)) {
            r[d.name] = it->second;
        } else if (it != values.end() && d.type == param_def::integer && std::holds_alternative<int64_t>(it->second)) {
            // out of range, e.g. edited by hand
            r[d.name] = std::clamp(std::get<int64_t>(it->second), d.min, d.max);
        } else {
            r[d.name] = d.def;
        }
    }
    return r;
}

void set_param(param_values& values, const param_def& def, param_value v)
{
    if (v == def.def)
        values.erase(def.name);
    else
        values[def.name] = std::move(v);
}

int64_t get_int(const param_values& v, const std::string& name)
{
    return std::get<int64_t>(v.at(name));
}

bool get_bool(const param_values& v, const std::string& name)
{
    return std::get<bool>(v.at(name));
}

const std::string& get_string(const param_values& v, const std::string& name)
{
    return std::get<std::string>(v.at(name));
}

std::string params_key(const param_values& v)
{
    std::string k;
    for (auto& [name, value] : v) {
        k += name;
        k += '=';
        if (auto i = std::get_if<int64_t>(&value))
            k += 'i' + std::to_string(*i);
        else if (auto b = std::get_if<bool>(&value))
            k += *b ? "b1" : "b0";
        else {
            // length prefix, the string may contain any character
            auto& s = std::get<std::string>(value);
            k += 's' + std::to_string(s.size()) + ':' + s;
        }
        k += ';';
    }
    return k;
}

}
