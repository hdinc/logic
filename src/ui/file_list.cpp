#include "file_list.h"
#include "icons.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace logic::ui {

namespace fs = std::filesystem;

namespace {

// a < b by name, ignoring case, with runs of digits compared as numbers
bool natural_less(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
            // the numbers without leading zeros, longer is larger
            while (i < a.size() && a[i] == '0') i++;
            while (j < b.size() && b[j] == '0') j++;
            size_t si = i, sj = j;
            while (i < a.size() && std::isdigit((unsigned char)a[i])) i++;
            while (j < b.size() && std::isdigit((unsigned char)b[j])) j++;
            if (i - si != j - sj) return i - si < j - sj;
            auto c = a.compare(si, i - si, b, sj, j - sj);
            if (c != 0) return c < 0;
            continue;
        }
        int ca = std::tolower((unsigned char)a[i]), cb = std::tolower((unsigned char)b[j]);
        if (ca != cb) return ca < cb;
        i++;
        j++;
    }
    if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
    // only case differs: a fixed order
    return a < b;
}

// a file time as local "2026-10-27 23:10"
std::string format_time(fs::file_time_type t)
{
    using namespace std::chrono;
    // file_clock's epoch isn't system_clock's everywhere
    auto sys = time_point_cast<system_clock::duration>(t - fs::file_time_type::clock::now() + system_clock::now());
    std::time_t tt = system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

}

std::string format_size(uintmax_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        u++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
    return buf;
}

void file_list::go(const fs::path& folder, const fs::path& select)
{
    dir = folder;
    selected = select;
    scroll_to_selected = !select.empty();
    stale = true;
}

void file_list::read()
{
    entries.clear();
    error.clear();
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        entry e;
        e.path = it->path();
        e.name = e.path.filename().string();
        // hidden files, like the system's file managers
        if (e.name.empty() || e.name[0] == '.') continue;
        std::error_code ignored;
        e.folder = it->is_directory(ignored);
        if (!e.folder && show && !show(e.path)) continue;
        if (!e.folder) e.size = it->file_size(ignored);
        auto t = it->last_write_time(ignored);
        if (!ignored) e.date = format_time(t);
        entries.push_back(std::move(e));
    }
    if (ec) error = ec.message();
    sort();
    // the selection only while it's still there
    if (!selected.empty() && selected.parent_path() == dir
        && std::none_of(entries.begin(), entries.end(), [&](auto& e) { return e.path == selected; }))
        selected.clear();
    listed = dir;
    read_at = ImGui::GetTime();
    stale = false;
}

void file_list::sort()
{
    std::stable_sort(entries.begin(), entries.end(), [&](const entry& a, const entry& b) {
        if (a.folder != b.folder) return a.folder;
        auto less = [&](const entry& x, const entry& y) {
            switch (sort_column) {
            case 1:
                if (x.size != y.size) return x.size < y.size;
                break;
            case 2:
                if (x.date != y.date) return x.date < y.date;
                break;
            }
            return natural_less(x.name, y.name);
        };
        return descending ? less(b, a) : less(a, b);
    });
}

void file_list::path_bar()
{
    using namespace ImGui;

    // from dir up to root (or the top)
    std::vector<fs::path> parts;
    for (auto p = dir; !p.empty(); p = p.parent_path()) {
        parts.push_back(p);
        if (p == root || p == p.parent_path()) break;
    }
    std::reverse(parts.begin(), parts.end());

    auto label = [](const fs::path& p) {
        auto name = p.filename().string();
        return name.empty() ? p.string() : name;
    };
    // the folders nearest to dir that fit, the ones before them behind "..."
    auto& style = GetStyle();
    float separator = CalcTextSize("/").x + 4.0f;
    auto button = [&](const std::string& s) { return CalcTextSize(s.c_str()).x + style.FramePadding.x * 2.0f; };
    float avail = GetContentRegionAvail().x;
    float width = 0.0f;
    size_t first = parts.size();
    while (first > 0) {
        float w = button(label(parts[first - 1])) + (first < parts.size() ? separator : 0.0f);
        // room for "..." unless it's the last one
        float more = first > 1 ? button("...") + separator : 0.0f;
        if (width + w + more > avail && first < parts.size()) break;
        width += w;
        first--;
    }

    if (first > 0) {
        if (SmallButton("...")) go(parts[first - 1], parts[first]);
        SetItemTooltip("%s", parts[first - 1].string().c_str());
    }
    for (size_t k = first; k < parts.size(); k++) {
        if (k > 0) {
            SameLine(0, 2);
            TextUnformatted("/");
            SameLine(0, 2);
        }
        PushID((int)k);
        if (SmallButton(label(parts[k]).c_str()) && parts[k] != dir) go(parts[k], k + 1 < parts.size() ? parts[k + 1] : fs::path{});
        PopID();
    }
}

bool file_list::table(fs::path& activated, float height)
{
    using namespace ImGui;

    if (stale || dir != listed || GetTime() - read_at > 1.0) read();
    if (!error.empty()) {
        TextDisabled("%s", error.c_str());
        return false;
    }

    bool chosen = false;
    auto flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit;
    if (!BeginTable("files", 3, flags, {0.0f, height})) return false;
    TableSetupScrollFreeze(0, 1);
    TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
    TableSetupColumn("Size", ImGuiTableColumnFlags_PreferSortDescending);
    TableSetupColumn("Modified", ImGuiTableColumnFlags_PreferSortDescending);
    // the date only where it leaves room for the names
    TableSetColumnEnabled(2, GetContentRegionAvail().x > GetFontSize() * 24.0f);
    TableHeadersRow();

    if (auto specs = TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount > 0) {
            sort_column = specs->Specs[0].ColumnIndex;
            descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        }
        sort();
        specs->SpecsDirty = false;
    }

    // a destination while drawing, taken after the table
    fs::path into;
    fs::path from;

    // where the names start in their column, after the wider icon
    float name_x = std::max(CalcTextSize(icon::folder).x, CalcTextSize(icon::file).x) + GetStyle().ItemInnerSpacing.x;

    if (dir != root && dir.has_parent_path() && dir.parent_path() != dir) {
        TableNextRow();
        TableNextColumn();
        auto at = GetCursorScreenPos();
        if (Selectable("##up", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) && IsMouseDoubleClicked(0)) {
            into = dir.parent_path();
            from = dir;
        }
        SetItemTooltip("Up one folder");
        SameLine();
        SetCursorScreenPos({at.x + name_x, at.y});
        TextUnformatted("..");
    }

    for (auto& e : entries) {
        TableNextRow();
        TableNextColumn();
        PushID(e.name.c_str());
        bool is_selected = e.path == selected;
        auto at = GetCursorScreenPos();
        if (Selectable("##entry", is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
            selected = e.path;
            if (IsMouseDoubleClicked(0)) {
                if (e.folder) {
                    into = e.path;
                } else {
                    activated = e.path;
                    chosen = true;
                }
            }
        }
        if (is_selected && scroll_to_selected) {
            SetScrollHereY();
            scroll_to_selected = false;
        }
        // the names line up whatever the icon's width
        SameLine();
        SetCursorScreenPos(at);
        TextUnformatted(e.folder ? icon::folder : icon::file);
        SameLine();
        SetCursorScreenPos({at.x + name_x, at.y});
        TextUnformatted(e.name.c_str());
        PopID();
        TableNextColumn();
        if (!e.folder) TextUnformatted(format_size(e.size).c_str());
        TableNextColumn();
        TextUnformatted(e.date.c_str());
    }
    if (entries.empty()) {
        TableNextRow();
        TableNextColumn();
        TextDisabled("empty");
    }
    EndTable();

    if (!into.empty()) go(into, from);
    return chosen;
}

}
