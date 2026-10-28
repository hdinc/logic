#include "file_manager.h"
#include "../ui/zoom.h"
#include "../editor/context.h"
#include "../ui/icons.h"
#include "../ui/theme.h"
#include "../web/web.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace logic::app {

namespace fs = std::filesystem;

namespace {

bool is_library(const fs::path& dir)
{
    std::error_code ec;
    return fs::is_regular_file(dir / "library.json", ec);
}

// false with an error if a new entry can't have that name
bool valid_name(const std::string& name, std::string& error)
{
    if (name.empty()) error = "the name is empty";
    else if (name == "." || name == "..") error = "'" + name + "' isn't a name";
    else if (name.find_first_of("/\\") != std::string::npos) error = "a name can't have / or \\";
    else return true;
    return false;
}

// a toolbar button, the next one goes on its line
bool tool(const char* icon, const char* tip, bool enabled = true)
{
    bool clicked = ui::icon_button(icon, tip, false, enabled);
    ImGui::SameLine(0, ui::toolbar_gap());
    return clicked;
}

}

file_manager::file_manager(const fs::path& r)
    : root(r)
{
    list.root = root;
    list.dir = root;
}

void file_manager::show(const fs::path& dir, const fs::path& file)
{
    open = true;
    list.go(dir, file);
    focus = true;
}

const char* file_manager::managed(const fs::path& p) const
{
    if (p.empty() || p == root) return "The workspace";
    if (is_library(p)) return "A library's folder, it's managed in the Libraries panel";
    // library.json, cells and bundles of a library, and what's in them
    for (auto q = p; q != root && q.has_parent_path() && q != q.parent_path(); q = q.parent_path()) {
        auto n = q.filename();
        if ((n == "library.json" || n == "cells" || n == "bundles") && is_library(q.parent_path()))
            return "Part of a library, it's managed in the Libraries panel";
    }
    return nullptr;
}

void file_manager::gui(const std::function<void(std::string)>& message)
{
    using namespace ImGui;

    if (!open) return;
    // with the libraries when it has no place yet (an older imgui.ini)
    if (auto lib = FindWindowByName("Libraries"); lib && lib->DockId) SetNextWindowDockID(lib->DockId, ImGuiCond_FirstUseEver);
    if (focus) {
        SetNextWindowFocus();
        focus = false;
    }
    if (!ui::zoom::begin("Files", &open)) {
        ui::zoom::end();
        return;
    }

    // a folder that's gone, e.g. deleted from its parent: up to one that's there
    std::error_code ec;
    while (list.dir != root && !fs::is_directory(list.dir, ec))
        list.dir = list.dir.parent_path();

    auto& sel = list.selected;
    bool have = !sel.empty() && fs::exists(sel, ec);
    bool file = have && fs::is_regular_file(sel, ec);
    auto locked = have ? managed(sel) : nullptr;

    if (tool(ui::icon::upload, "Upload files from your computer into this folder, replacing ones with the same names. Dropping files on the page does the same."))
        web::upload(list.dir);
    if (tool(ui::icon::new_folder, "New folder")) {
        ask = asking::new_folder;
        name.clear();
        error.clear();
        focus_name = true;
    }
    if (tool(ui::icon::download, file ? "Download the file to your computer" : "Download: choose a file", file))
        web::download(sel);
    if (tool(ui::icon::rename, locked ? locked : have ? "Rename" : "Rename: choose a file or folder", have && !locked)) {
        ask = asking::rename;
        target = sel;
        name = sel.filename().string();
        error.clear();
        focus_name = true;
    }
    if (tool(ui::icon::trash, locked ? locked : have ? "Delete" : "Delete: choose a file or folder", have && !locked)) {
        ask = asking::remove;
        target = sel;
    }
    NewLine();

    list.path_bar();
    fs::path activated;
    // a double clicked file: nothing to open it with, download it
    if (list.table(activated)) web::download(activated);

    popups(message);
    ui::zoom::end();
}

void file_manager::popups(const std::function<void(std::string)>& message)
{
    using namespace ImGui;

    if (ask == asking::new_folder) OpenPopup("New folder");
    if (ask == asking::rename) OpenPopup("Rename");
    if (ask == asking::remove) OpenPopup("Delete");

    // the name of a new folder or a renamed entry, true once it's accepted
    auto name_input = [&](const char* button) {
        if (focus_name) {
            SetKeyboardFocusHere();
            focus_name = false;
        }
        SetNextItemWidth(GetFontSize() * 20.0f);
        editor::input_string("##name", name, ImGuiInputTextFlags_AutoSelectAll);
        bool enter = IsItemDeactivated() && (IsKeyPressed(ImGuiKey_Enter) || IsKeyPressed(ImGuiKey_KeypadEnter));
        if (!error.empty()) TextColored(ui::color(ui::color_id::error), "%s", error.c_str());
        bool ok = Button(button) || enter;
        SameLine();
        if (Button("Cancel") || IsKeyPressed(ImGuiKey_Escape)) {
            ask = asking::none;
            CloseCurrentPopup();
            return false;
        }
        if (!ok) return false;
        if (!valid_name(name, error)) {
            focus_name = true;
            return false;
        }
        return true;
    };

    if (BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("A folder in %s", list.dir.filename().string().c_str());
        if (name_input("Create")) {
            auto p = list.dir / name;
            std::error_code ec;
            if (fs::exists(p, ec)) {
                error = "'" + name + "' is already there";
                focus_name = true;
            } else if (!fs::create_directory(p, ec)) {
                error = ec.message();
                focus_name = true;
            } else {
                list.go(list.dir, p);
                ask = asking::none;
                CloseCurrentPopup();
            }
        }
        EndPopup();
    }

    if (BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Text("Rename '%s' to", target.filename().string().c_str());
        if (name_input("Rename")) {
            auto p = target.parent_path() / name;
            std::error_code ec;
            if (p == target) {
                ask = asking::none;
                CloseCurrentPopup();
            } else if (fs::exists(p, ec)) {
                error = "'" + name + "' is already there";
                focus_name = true;
            } else {
                fs::rename(target, p, ec);
                if (ec) {
                    error = ec.message();
                    focus_name = true;
                } else {
                    message("renamed " + target.string() + " to " + name);
                    list.go(list.dir, p);
                    ask = asking::none;
                    CloseCurrentPopup();
                }
            }
        }
        EndPopup();
    }

    if (BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::error_code ec;
        bool folder = fs::is_directory(target, ec);
        Text(folder ? "Delete the folder '%s' and everything in it?" : "Delete '%s'?", target.filename().string().c_str());
        TextDisabled("This can't be undone.");
        if (Button("Delete")) {
            fs::remove_all(target, ec);
            if (ec)
                message("deleting " + target.string() + " failed: " + ec.message());
            else
                message("deleted " + target.string());
            list.selected.clear();
            list.refresh();
            ask = asking::none;
            CloseCurrentPopup();
        }
        SameLine();
        if (Button("Cancel") || IsKeyPressed(ImGuiKey_Escape)) {
            ask = asking::none;
            CloseCurrentPopup();
        }
        EndPopup();
    }
}

}
