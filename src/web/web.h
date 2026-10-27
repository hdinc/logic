#pragma once

// glue between the program and its page, in the web build only

#include <filesystem>
#include <vector>

namespace logic::web {

// the folder the page keeps in the browser's IndexedDB, see web/pre.js
constexpr const char* home = "/home/web_user";

// the page asks before it's closed while there are unsaved changes
void set_unsaved(bool unsaved);

// opens the browser's file picker and copies the chosen files into dir,
// replacing ones with the same names. call it right after a click, browsers
// only open the picker then.
void upload(const std::filesystem::path& dir);
// files dropped on the page are copied into dir
void set_drop_dir(const std::filesystem::path& dir);
// the files uploaded or dropped since the last call, once they're written
std::vector<std::filesystem::path> take_uploaded();

// the browser saves the file to the user's computer
void download(const std::filesystem::path& file);

}
