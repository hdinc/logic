#pragma once

// glue between the program and its page, in the web build only

namespace logic::web {

// the folder the page keeps in the browser's IndexedDB, see web/pre.js
constexpr const char* home = "/home/web_user";

// the page asks before it's closed while there are unsaved changes
void set_unsaved(bool unsaved);

}
