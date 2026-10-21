#include "viewer_app.h"

#include <Hd/Window.h>

#include <cstdio>
#include <exception>

// usage: logic-wave [file.vcd | session.json]
int main(int argc, char** argv)
{
    try {
        Hd::Window::setName("logic-wave");
        Hd::Window::setSize(1600, 900);
        Hd::Window& window = Hd::Window::getInstance();
        window.VSync(true);

        logic::viewer::viewer_app app(window);
        if (argc > 1) app.open(argv[1]);
        app.run();
    } catch (std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
