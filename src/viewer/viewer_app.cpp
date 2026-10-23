#include <glad/glad.h>

#include "viewer_app.h"
#include "../ui/file_dialog.h"
#include "../ui/fonts.h"
#include "../ui/theme.h"

#include <Hd/Resource.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace logic::viewer {

namespace fs = std::filesystem;

namespace {

viewer_app* drop_target = nullptr;

const char* const open_vcd_dialog = "open_vcd";
const char* const open_session_dialog = "open_session";
const char* const save_session_dialog = "save_session";

// a file's rows are picked by the user
class file_host : public wave::view_host {};

bool contains_nocase(std::string_view text, std::string_view part)
{
    auto it = std::search(text.begin(), text.end(), part.begin(), part.end(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != text.end();
}

}

viewer_app::viewer_app(Hd::Window& w)
    : window(w)
    , gui(&w)
{
    auto& io = ImGui::GetIO();
    // not imgui.ini, the logic app keeps its settings there
    io.IniFilename = "logic-wave.ini";

    auto f = Hd::Resource::getExeRes("res/fonts/Roboto.ttf");
    std::vector<std::string> errors;
    ui::load_fonts({}, {}, {(const char*)f.first, (size_t)f.second}, errors);
    ui::set_font_size(20.0f);

    add_settings_handler();
    apply_theme();
    gui.Functions.add([this] { draw_gui(); });

    drop_target = this;
    glfwSetDropCallback(window.Id(), [](GLFWwindow*, int count, const char** paths) {
        if (count > 0 && drop_target) drop_target->open(paths[0]);
    });
    set_title();
}

viewer_app::~viewer_app()
{
    // imgui saves the settings when gui is destroyed, after the members
    // the settings handler reads
    auto& io = ImGui::GetIO();
    if (io.IniFilename) ImGui::SaveIniSettingsToDisk(io.IniFilename);
    io.IniFilename = nullptr;
    drop_target = nullptr;
    if (job) {
        job->progress.cancel = true;
        job->thread.join();
    }
}

void viewer_app::run()
{
    while (!should_exit) {
        window.PollEvents();
        if (window.ShouldClose()) should_exit = true;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (job && job->done) finish_loading();
        gui.Draw();
        window.SwapBuffers();
    }
}

void viewer_app::open(const fs::path& path)
{
    std::error_code ec;
    if (path.extension() == ".json") {
        try {
            auto s = wave::load_session(path);
            fs::path vcd = s.file;
            if (vcd.is_relative()) vcd = path.parent_path() / vcd;
            session_file = path;
            load(vcd, std::move(s));
        } catch (std::exception& e) {
            error(e.what());
        }
        return;
    }
    session_file.clear();
    load(path, std::nullopt);
}

void viewer_app::load(const fs::path& vcd, std::optional<wave::session> session)
{
    if (job) return;
    job = std::make_unique<load_job>();
    job->file = fs::absolute(vcd);
    job->session = std::move(session);
    auto j = job.get();
    j->thread = std::thread([j] {
        try {
            j->result = wave::load_vcd(j->file, &j->progress);
        } catch (std::exception& e) {
            j->error = e.what();
        }
        j->done = true;
    });
}

void viewer_app::finish_loading()
{
    auto j = std::move(job);
    j->thread.join();
    if (!j->result) {
        if (!j->progress.cancel) error("Couldn't load " + j->file.string() + "\n" + j->error);
        return;
    }
    data = std::move(*j->result);
    file = j->file;
    build_tree();
    notes.clear();
    if (j->session) {
        auto missing = view.restore(data, *j->session);
        for (auto& name : missing)
            notes.push_back("Not in the file: " + name);
    } else {
        view.clear();
        view.fit_all();
    }
    set_title();
}

void viewer_app::reload()
{
    if (file.empty()) return;
    // the rows by name, so they stay when signals come and go
    load(file, view.save(data));
}

void viewer_app::save_session(const fs::path& path)
{
    auto s = view.save(data);
    std::error_code ec;
    auto rel = fs::relative(file, path.parent_path(), ec);
    s.file = (ec || rel.empty() ? file : rel).generic_string();
    try {
        wave::save_session(path, s);
        session_file = path;
        notes.push_back("Saved " + path.string());
    } catch (std::exception& e) {
        error(e.what());
    }
}

void viewer_app::build_tree()
{
    tree = {};
    for (int k = 0; k < (int)data.signals.size(); k++) {
        auto scope = data.signals[k].scope();
        auto* node = &tree;
        size_t start = 0;
        while (start < scope.size()) {
            size_t sep = scope.find_first_of("/.", start);
            if (sep == std::string_view::npos) sep = scope.size();
            auto part = scope.substr(start, sep - start);
            auto it = std::find_if(node->children.begin(), node->children.end(), [&](auto& c) { return c.name == part; });
            if (it == node->children.end()) {
                node->children.push_back({std::string(part), {}, {}});
                node = &node->children.back();
            } else {
                node = &*it;
            }
            start = sep + 1;
        }
        node->signals.push_back(k);
    }
}

void viewer_app::add_all(const scope_node& node)
{
    for (int k : node.signals)
        view.add(data, data.signals[k].key);
    for (auto& c : node.children)
        add_all(c);
}

void viewer_app::signal_item(const wave::signal& s)
{
    using namespace ImGui;
    std::string label(s.leaf());
    if (s.type == wave::kind::real) label += "  real";
    else if (s.type == wave::kind::text) label += "  text";
    else if (s.width > 1 && label.find('[') == std::string::npos) label += "  [" + std::to_string(s.width) + "]";
    PushID((int)s.key);
    if (Selectable(label.c_str(), view.has(s.key), ImGuiSelectableFlags_AllowDoubleClick) && IsMouseDoubleClicked(ImGuiMouseButton_Left))
        view.add(data, s.key);
    if (BeginDragDropSource()) {
        SetDragDropPayload(wave::view::signal_payload, &s.key, sizeof(s.key));
        TextUnformatted(s.name.c_str());
        EndDragDropSource();
    }
    SetItemTooltip("%s\nDouble-click or drag to the waveforms to add it", s.name.c_str());
    PopID();
}

void viewer_app::draw_scope(const scope_node& node)
{
    using namespace ImGui;
    for (auto& c : node.children) {
        bool open = TreeNodeEx(c.name.c_str(), &c == &tree.children.front() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (BeginPopupContextItem()) {
            if (MenuItem("Add all signals")) add_all(c);
            EndPopup();
        }
        if (open) {
            draw_scope(c);
            TreePop();
        }
    }
    for (int k : node.signals)
        signal_item(data.signals[k]);
}

void viewer_app::signals_panel()
{
    using namespace ImGui;
    if (file.empty()) {
        TextWrapped("Open a VCD file with File > Open (Ctrl+O), or drop one on the window.");
        return;
    }
    TextUnformatted(file.filename().string().c_str());
    SetItemTooltip("%s", file.string().c_str());
    TextDisabled("%zu signals, %s - %s", data.signals.size(), wave::format_time(data.begin, data.tb).c_str(),
        wave::format_time(data.end, data.tb).c_str());

    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", filter.c_str());
    SetNextItemWidth(-FLT_MIN);
    if (InputTextWithHint("##filter", "Filter signals", buf, sizeof(buf))) filter = buf;

    BeginChild("signals", {0, 0}, ImGuiChildFlags_Borders);
    if (filter.empty()) {
        draw_scope(tree);
    } else {
        std::vector<int> matching;
        for (int k = 0; k < (int)data.signals.size(); k++)
            if (contains_nocase(data.signals[k].name, filter)) matching.push_back(k);
        if (SmallButton(("Add all " + std::to_string(matching.size())).c_str()))
            for (int k : matching)
                view.add(data, data.signals[k].key);
        ImGuiListClipper clipper;
        clipper.Begin((int)matching.size());
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                auto& s = data.signals[matching[i]];
                signal_item(s);
                SameLine();
                TextDisabled("%s", std::string(s.scope()).c_str());
            }
    }
    EndChild();
}

void viewer_app::draw_gui()
{
    using namespace ImGui;
    auto vp = GetMainViewport();
    SetNextWindowPos(vp->WorkPos);
    SetNextWindowSize(vp->WorkSize);
    PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    Begin("##main", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    PopStyleVar();
    menu_bar();

    if (BeginTable("layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, GetContentRegionAvail())) {
        TableSetupColumn("signals", ImGuiTableColumnFlags_WidthFixed, GetFontSize() * 16);
        TableSetupColumn("waveforms", ImGuiTableColumnFlags_WidthStretch);
        TableNextColumn();
        BeginChild("signals_panel", {0, GetContentRegionAvail().y});
        signals_panel();
        EndChild();
        TableNextColumn();
        BeginChild("waveforms_panel", {0, GetContentRegionAvail().y});
        int dismiss = -1;
        for (int k = 0; k < (int)notes.size(); k++) {
            PushID(k);
            if (SmallButton("x")) dismiss = k;
            SameLine();
            TextDisabled("%s", notes[k].c_str());
            PopID();
        }
        if (dismiss >= 0) notes.erase(notes.begin() + dismiss);
        file_host host;
        view.draw(data, host);
        EndChild();
        EndTable();
    }
    End();

    loading_popup();
    dialogs();
    shortcuts();

    if (!error_message.empty() && !IsPopupOpen("Error")) OpenPopup("Error");
    if (BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        TextUnformatted(error_message.c_str());
        if (Button("OK") || IsKeyPressed(ImGuiKey_Enter)) {
            error_message.clear();
            CloseCurrentPopup();
        }
        EndPopup();
    }

    if (show_help) {
        SetNextWindowSize({560, 0}, ImGuiCond_FirstUseEver);
        if (Begin("Shortcuts", &show_help, ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* keys[][2] = {
                {"Click / Shift+click", "Cursor A / cursor B, snapping to transitions (Alt: no snapping)"},
                {"Left / Right", "Previous / next transition of the selected signals"},
                {"Shift+Left / Right", "The same for cursor B"},
                {"Wheel", "Zoom about the mouse"},
                {"Right or middle drag", "Pan"},
                {"Drag over the ruler", "Zoom to that range"},
                {"+ / - / F", "Zoom in / out / fit everything"},
                {"M, Ctrl+[ / ]", "Marker at cursor A, previous / next marker"},
                {"Delete", "Remove the selected rows"},
                {"Escape", "Clear cursor B"},
                {"Ctrl+O / Ctrl+R / Ctrl+S", "Open / reload / save session"},
            };
            if (BeginTable("keys", 2, ImGuiTableFlags_RowBg)) {
                for (auto& k : keys) {
                    TableNextColumn();
                    TextUnformatted(k[0]);
                    TableNextColumn();
                    TextUnformatted(k[1]);
                }
                EndTable();
            }
        }
        End();
    }
}

void viewer_app::menu_bar()
{
    using namespace ImGui;
    if (!BeginMenuBar()) return;
    if (BeginMenu("File")) {
        if (MenuItem("Open...", "Ctrl+O")) ui::open_dialog(open_vcd_dialog, "Open a VCD file", ui::waveform_files, file.empty() ? fs::current_path() : file.parent_path());
        if (MenuItem("Reload", "Ctrl+R", false, !file.empty())) reload();
        Separator();
        if (MenuItem("Open session...")) ui::open_dialog(open_session_dialog, "Open a session", ui::session_files, file.empty() ? fs::current_path() : file.parent_path());
        if (MenuItem("Save session", "Ctrl+S", false, !file.empty())) {
            if (session_file.empty()) ui::open_dialog(save_session_dialog, "Save the session", ui::session_files, file.parent_path() / (file.stem().string() + ".json"), true);
            else save_session(session_file);
        }
        if (MenuItem("Save session as...", nullptr, false, !file.empty()))
            ui::open_dialog(save_session_dialog, "Save the session", ui::session_files, session_file.empty() ? file.parent_path() / (file.stem().string() + ".json") : session_file, true);
        Separator();
        if (MenuItem("Quit", "Ctrl+Q")) should_exit = true;
        EndMenu();
    }
    if (BeginMenu("View")) {
        if (BeginMenu("Theme")) {
            // the logic app's themes, edited there
            if (ui::theme_menu_items(theme_name)) {
                apply_theme();
                MarkIniSettingsDirty();
            }
            EndMenu();
        }
        if (MenuItem("Remove all rows", nullptr, false, view.rows() > 0)) view.clear();
        EndMenu();
    }
    if (BeginMenu("Help")) {
        MenuItem("Shortcuts", nullptr, &show_help);
        EndMenu();
    }
    EndMenuBar();
}

void viewer_app::dialogs()
{
    fs::path chosen;
    if (ui::dialog_result(open_vcd_dialog, chosen)) open(chosen);
    if (ui::dialog_result(open_session_dialog, chosen)) open(chosen);
    if (ui::dialog_result(save_session_dialog, chosen)) {
        if (chosen.extension().empty()) chosen += ".json";
        save_session(chosen);
    }
}

void viewer_app::loading_popup()
{
    using namespace ImGui;
    if (job && !IsPopupOpen("Loading")) OpenPopup("Loading");
    if (BeginPopupModal("Loading", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!job) {
            CloseCurrentPopup();
        } else {
            TextUnformatted(job->file.filename().string().c_str());
            ProgressBar(job->progress.fraction, {GetFontSize() * 20, 0});
            if (Button("Cancel")) job->progress.cancel = true;
        }
        EndPopup();
    }
}

void viewer_app::shortcuts()
{
    using namespace ImGui;
    auto& io = GetIO();
    if (io.WantTextInput || !io.KeyCtrl) return;
    if (IsKeyPressed(ImGuiKey_O, false)) ui::open_dialog(open_vcd_dialog, "Open a VCD file", ui::waveform_files, file.empty() ? fs::current_path() : file.parent_path());
    if (IsKeyPressed(ImGuiKey_R, false)) reload();
    if (IsKeyPressed(ImGuiKey_S, false) && !file.empty()) {
        if (session_file.empty()) ui::open_dialog(save_session_dialog, "Save the session", ui::session_files, file.parent_path() / (file.stem().string() + ".json"), true);
        else save_session(session_file);
    }
    if (IsKeyPressed(ImGuiKey_Q, false)) should_exit = true;
}

void viewer_app::add_settings_handler()
{
    ImGuiSettingsHandler h;
    h.TypeName = "LogicWave";
    h.TypeHash = ImHashStr("LogicWave");
    h.UserData = this;
    h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
        return std::strcmp(name, "Settings") == 0 ? (void*)1 : nullptr;
    };
    h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* h, void*, const char* line) {
        auto a = (viewer_app*)h->UserData;
        // the rest of the line, names of custom themes may have spaces
        if (std::strncmp(line, "Theme=", 6) == 0) a->theme_name = line + 6;
    };
    h.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h) { ((viewer_app*)h->UserData)->apply_theme(); };
    h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
        auto a = (viewer_app*)h->UserData;
        buf->appendf("[LogicWave][Settings]\nTheme=%s\n\n", a->theme_name.c_str());
    };
    ImGui::AddSettingsHandler(&h);
}

void viewer_app::apply_theme()
{
    ui::theme t;
    std::string error;
    if (!ui::find_theme(theme_name, t, error)) {
        notes.push_back("Theme '" + theme_name + "': " + error + ", using Dark");
        theme_name = "dark";
    }
    ui::set_current(std::move(t));
    ui::apply_current();
    auto bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(bg.x, bg.y, bg.z, 1);
}

void viewer_app::set_title()
{
    auto title = file.empty() ? std::string("logic-wave") : file.filename().string() + " - logic-wave";
    glfwSetWindowTitle(window.Id(), title.c_str());
}

void viewer_app::error(std::string message)
{
    error_message = std::move(message);
}

}
