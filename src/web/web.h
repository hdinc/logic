#pragma once

// glue between the program and its page, in the web build only

#include <filesystem>
#include <vector>

namespace logic::web {

// the folder the page keeps in the browser's IndexedDB, see web/pre.js
constexpr const char* home = "/home/web_user";

// the page asks before it's closed while there are unsaved changes
void set_unsaved(bool unsaved);

// opens the browser's file picker and copies the chosen file into dir,
// replacing one with the same name. call it right after a click, browsers
// only open the picker then. take_upload gives the file once it's written,
// id tells the callers apart.
void upload(unsigned id, const std::filesystem::path& dir);
bool take_upload(unsigned id, std::filesystem::path& file);

// files dropped on the page are copied into dir
void set_drop_dir(const std::filesystem::path& dir);
// the files dropped since the last call
std::vector<std::filesystem::path> take_dropped();

}
