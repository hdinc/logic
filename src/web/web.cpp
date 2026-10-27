#include "web.h"

#include <emscripten.h>

#include <cstdlib>

namespace logic::web {

namespace {

EM_JS(void, js_set_unsaved, (int unsaved), {
    Module.logicUnsaved = !!unsaved;
});

EM_JS(void, js_upload, (const char* dir), {
    Module.logicUpload(UTF8ToString(dir));
});

EM_JS(void, js_set_drop_dir, (const char* dir), {
    Module.logicDropDir = UTF8ToString(dir);
});

// the next uploaded file, null if there's none. freed by the caller.
EM_JS(char*, js_take_uploaded, (), {
    var f = Module.logicUploaded.shift();
    return f === undefined ? 0 : stringToNewUTF8(f);
});

EM_JS(void, js_download, (const char* file), {
    Module.logicDownload(UTF8ToString(file));
});

}

void set_unsaved(bool unsaved)
{
    static int last = -1;
    if (last == (int)unsaved) return;
    last = unsaved;
    js_set_unsaved(unsaved);
}

void upload(const std::filesystem::path& dir)
{
    js_upload(dir.c_str());
}

void set_drop_dir(const std::filesystem::path& dir)
{
    static std::filesystem::path last;
    if (dir == last) return;
    last = dir;
    js_set_drop_dir(dir.c_str());
}

std::vector<std::filesystem::path> take_uploaded()
{
    std::vector<std::filesystem::path> r;
    while (auto s = js_take_uploaded()) {
        r.push_back(s);
        std::free(s);
    }
    return r;
}

void download(const std::filesystem::path& file)
{
    js_download(file.c_str());
}

}
