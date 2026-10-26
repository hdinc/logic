#include "web.h"

#include <emscripten.h>

namespace logic::web {

namespace {

EM_JS(void, js_set_unsaved, (int unsaved), {
    Module.logicUnsaved = !!unsaved;
});

}

void set_unsaved(bool unsaved)
{
    static int last = -1;
    if (last == (int)unsaved) return;
    last = unsaved;
    js_set_unsaved(unsaved);
}

}
