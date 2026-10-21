#include "file_dialog.h"

#include <ImGuiFileDialog.h>
#include <imgui.h>

namespace logic::ui {

namespace fs = std::filesystem;

const char* const firmware_files = "Firmware and memory contents{.hex,.ihx,.ihex,.srec,.s19,.s28,.s37,.mot,.elf,.bin},.*";
const char* const hdl_files = "HDL sources{.v,.sv,.vh,.svh,.vhd,.vhdl},.*";
#if defined(_WIN32)
const char* const plugin_files = "Plugins{.dll},.*";
#else
const char* const plugin_files = "Plugins{.so},.*";
#endif
const char* const waveform_files = "Value change dumps{.vcd},.*";
const char* const session_files = "Waveform sessions{.json},.*";

namespace {

// p relative to base if it's inside it
std::string relative_if_inside(const fs::path& p, const fs::path& base)
{
    if (base.empty()) return p.generic_string();
    std::error_code ec;
    auto rel = fs::relative(p, base, ec);
    if (ec || rel.empty() || *rel.begin() == "..") return p.generic_string();
    return rel.generic_string();
}

bool display(const std::string& key, fs::path& file)
{
    bool chosen = false;
    auto& vp = *ImGui::GetMainViewport();
    if (ImGuiFileDialog::Instance()->Display(key, ImGuiWindowFlags_NoCollapse, {vp.Size.x * 0.5f, vp.Size.y * 0.5f})) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            file = ImGuiFileDialog::Instance()->GetFilePathName();
            chosen = true;
        }
        ImGuiFileDialog::Instance()->Close();
    }
    return chosen;
}

}

bool browse_button(const char* id, std::string& path, const fs::path& base, const char* filters, const char* title)
{
    using namespace ImGui;

    PushID(id);
    // one dialog per button
    auto key = "file_dialog_" + std::to_string(GetID("dialog"));

    if (Button("...")) {
        IGFD::FileDialogConfig config;
        std::error_code ec;
        fs::path current = path.empty() ? fs::path{} : fs::path(path);
        if (!current.empty() && current.is_relative()) current = base / current;
        fs::path dir = current.empty() ? base : current.parent_path();
        if (dir.empty() || !fs::is_directory(dir, ec)) dir = base.empty() || !fs::is_directory(base, ec) ? fs::current_path(ec) : base;
        config.path = dir.string();
        if (!current.empty()) config.fileName = current.filename().string();
        config.flags = ImGuiFileDialogFlags_Modal;
        ImGuiFileDialog::Instance()->OpenDialog(key, title, filters, config);
    }
    SetItemTooltip("Choose a file");

    fs::path file;
    bool chosen = display(key, file);
    if (chosen) path = relative_if_inside(file, base);
    PopID();
    return chosen;
}

void open_dialog(const char* id, const char* title, const char* filters, const fs::path& start, bool save)
{
    IGFD::FileDialogConfig config;
    std::error_code ec;
    fs::path dir = fs::is_directory(start, ec) ? start : start.parent_path();
    if (dir.empty() || !fs::is_directory(dir, ec)) dir = fs::current_path(ec);
    config.path = dir.string();
    if (!fs::is_directory(start, ec) && start.has_filename()) config.fileName = start.filename().string();
    config.flags = ImGuiFileDialogFlags_Modal;
    if (save) config.flags |= ImGuiFileDialogFlags_ConfirmOverwrite;
    ImGuiFileDialog::Instance()->OpenDialog(id, title, filters, config);
}

bool dialog_result(const char* id, fs::path& file)
{
    return display(id, file);
}

}
