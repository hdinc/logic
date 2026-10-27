#include "web.h"

#include <emscripten.h>

#include <cstdlib>

namespace logic::web {

namespace {

EM_JS(void, js_set_unsaved, (int unsaved), {
    Module.logicUnsaved = !!unsaved;
});

EM_JS(void, js_upload, (unsigned id, const char* dir), {
    Module.logicUpload(id, UTF8ToString(dir));
});

// the path of a finished upload, null if there's none. freed by the caller.
EM_JS(char*, js_take_upload, (unsigned id), {
    var f = Module.logicUploads[id];
    if (f === undefined) return 0;
    delete Module.logicUploads[id];
    return stringToNewUTF8(f);
});

EM_JS(void, js_set_drop_dir, (const char* dir), {
    Module.logicDropDir = UTF8ToString(dir);
});

// the next dropped file, null if there's none. freed by the caller.
EM_JS(char*, js_take_dropped, (), {
    var f = Module.logicDropped.shift();
    return f === undefined ? 0 : stringToNewUTF8(f);
});

// a string from js, null for none
bool take(char* s, std::filesystem::path& out)
{
    if (!s) return false;
    out = s;
    std::free(s);
    return true;
}

}

void set_unsaved(bool unsaved)
{
    static int last = -1;
    if (last == (int)unsaved) return;
    last = unsaved;
    js_set_unsaved(unsaved);
}

void upload(unsigned id, const std::filesystem::path& dir)
{
    js_upload(id, dir.c_str());
}

bool take_upload(unsigned id, std::filesystem::path& file)
{
    return take(js_take_upload(id), file);
}

void set_drop_dir(const std::filesystem::path& dir)
{
    static std::filesystem::path last;
    if (dir == last) return;
    last = dir;
    js_set_drop_dir(dir.c_str());
}

std::vector<std::filesystem::path> take_dropped()
{
    std::vector<std::filesystem::path> r;
    std::filesystem::path f;
    while (take(js_take_dropped(), f))
        r.push_back(f);
    return r;
}

}
