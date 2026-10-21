// choosing files with ImGuiFileDialog

#pragma once

#include <filesystem>
#include <string>

namespace logic::ui {

// filters for the dialogs, in ImGuiFileDialog's syntax
extern const char* const firmware_files;
extern const char* const hdl_files;
extern const char* const plugin_files;
extern const char* const waveform_files;
extern const char* const session_files;

// a "..." button that opens a file dialog at path. true when a file was
// chosen, path is then that file: relative to base if it's inside base,
// absolute otherwise. call it every frame while the dialog may be open, it
// draws the dialog too.
bool browse_button(const char* id, std::string& path, const std::filesystem::path& base, const char* filters, const char* title = "Choose a file");

// a dialog to open or save a file, started by open_dialog and drawn by
// dialog_result every frame. dialog_result is true once a file was chosen,
// file is then its absolute path. saving asks before overwriting.
void open_dialog(const char* id, const char* title, const char* filters, const std::filesystem::path& start, bool save = false);
bool dialog_result(const char* id, std::filesystem::path& file);

}
