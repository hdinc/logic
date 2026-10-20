// choosing files with ImGuiFileDialog

#pragma once

#include <filesystem>
#include <string>

namespace logic::editor {

// filters for browse_button, in ImGuiFileDialog's syntax
extern const char* const firmware_files;
extern const char* const hdl_files;
extern const char* const plugin_files;

// a "..." button that opens a file dialog at path. true when a file was
// chosen, path is then that file: relative to base if it's inside base,
// absolute otherwise. call it every frame while the dialog may be open, it
// draws the dialog too.
bool browse_button(const char* id, std::string& path, const std::filesystem::path& base, const char* filters, const char* title = "Choose a file");

}
