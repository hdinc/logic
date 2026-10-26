#include "app/app.h"

#include <Hd/Window.h>

#include <cstdio>
#include <exception>

#ifdef __EMSCRIPTEN__
#include "web/web.h"
#include <unistd.h>
#endif

// usage: logic [workspace folder]
// without an argument the workspace folder in the repository is used
int main(int argc, char** argv)
{
    std::filesystem::path root = argc > 1 ? argv[1] : LOGIC_DEFAULT_WORKSPACE;

    try {
        Hd::Window::setName("logic");
        Hd::Window::setSize(1920, 1080);
        Hd::Window& window = Hd::Window::getInstance();
        window.VSync(true);

#ifdef __EMSCRIPTEN__
        // the workspace and imgui.ini are kept in the browser
        chdir(logic::web::home);
        std::filesystem::create_directories(root);
        // run returns and the browser runs the frames, the app lives on
        auto a = new logic::app::app(window, root);
        a->run();
#else
        logic::app::app a(window, root);
        a.run();
#endif
    } catch (std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    return 0;
}
