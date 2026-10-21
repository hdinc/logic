#include "session.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace logic::wave {

using json = nlohmann::json;

namespace {

const char* display_ids[] = {"digital", "step", "linear"};

}

std::string session_to_json(const session& s)
{
    json j;
    j["format"] = "logic-wave session";
    j["version"] = 1;
    j["file"] = s.file;
    j["default_format"] = format_id(s.default_format);
    j["full_path"] = s.full_path;
    j["view"] = {{"start", s.view_start}, {"span", s.view_span}};
    if (s.cursor_a) j["cursor_a"] = *s.cursor_a;
    if (s.cursor_b) j["cursor_b"] = *s.cursor_b;
    auto& rows = j["rows"] = json::array();
    for (auto& r : s.rows) {
        json row = {{"name", r.name}, {"display", display_ids[int(r.mode)]}, {"height", r.height}};
        if (r.fmt) row["format"] = format_id(*r.fmt);
        if (r.expanded) row["expanded"] = true;
        if (r.color) row["color"] = r.color;
        rows.push_back(std::move(row));
    }
    auto& markers = j["markers"] = json::array();
    for (auto& m : s.markers)
        markers.push_back({{"name", m.name}, {"time", m.t}});
    return j.dump(2);
}

session session_from_json(std::string_view text)
{
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != "logic-wave session")
        throw std::runtime_error("not a waveform session file");

    session s;
    try {
        s.file = j.value("file", "");
        s.default_format = parse_format_id(j.value("default_format", "hex")).value_or(format::hex);
        s.full_path = j.value("full_path", true);
        if (j.contains("view")) {
            s.view_start = j["view"].value("start", 0.0);
            s.view_span = j["view"].value("span", 0.0);
        }
        if (j.contains("cursor_a")) s.cursor_a = j["cursor_a"].get<time>();
        if (j.contains("cursor_b")) s.cursor_b = j["cursor_b"].get<time>();
        for (auto& row : j.value("rows", json::array())) {
            row_state r;
            r.name = row.value("name", "");
            if (row.contains("format")) r.fmt = parse_format_id(row["format"].get<std::string>());
            auto mode = row.value("display", "digital");
            for (int k = 0; k < 3; k++)
                if (mode == display_ids[k]) r.mode = display(k);
            r.height = row.value("height", 1.0f);
            r.expanded = row.value("expanded", false);
            r.color = row.value("color", uint32_t(0));
            if (!r.name.empty()) s.rows.push_back(std::move(r));
        }
        for (auto& m : j.value("markers", json::array()))
            s.markers.push_back({m.value("name", ""), m.value("time", time(0))});
    } catch (json::exception& e) {
        throw std::runtime_error(std::string("bad session file: ") + e.what());
    }
    return s;
}

void save_session(const std::filesystem::path& file, const session& s)
{
    std::ofstream out(file);
    out << session_to_json(s) << "\n";
    if (!out) throw std::runtime_error("can't write " + file.string());
}

session load_session(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) throw std::runtime_error("can't open " + file.string());
    std::stringstream text;
    text << in.rdbuf();
    return session_from_json(text.str());
}

}
