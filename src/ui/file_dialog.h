// choosing files: a modal dialog over ui::file_list

#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace logic::ui {

// filters for the dialogs: comma separated, each a list of extensions with
// a description, "Description{.a,.b}", one extension ".a", or ".*" for all
// files. the first one is chosen at the start.
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
// file is then its absolute path. saving asks before overwriting and adds
// the filter's extension to a name without one.
void open_dialog(const char* id, const char* title, const char* filters, const std::filesystem::path& start, bool save = false);
bool dialog_result(const char* id, std::filesystem::path& file);

// the dialogs don't go above root (none if empty), and its Home button goes
// there. the browser keeps them in the workspace.
void set_dialog_root(const std::filesystem::path& root);
// an upload button in the dialogs, upload(dir) copies files from the user's
// computer into dir. the browser has one.
void set_dialog_upload(std::function<void(const std::filesystem::path&)> upload);
// the folder of the open dialog, empty if none is open
std::filesystem::path dialog_folder();
// selects a file that was just uploaded in the open dialog. false if none
// is open.
bool dialog_show_file(const std::filesystem::path& file);

}
