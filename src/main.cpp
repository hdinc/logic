#include <Hd.h>
#include "logic.h"

int main()
{
    Hd::Window::setSize(1920, 1080);
    Hd::Window& window = Hd::Window::getInstance();
    // TODO: make drawing seperate thread from input handlign
    window.VSync(true);
    // window.fpsLimit(200);
    logic::logic l(&window);
    l.exec();

    return 0;
}

// TODO: Hd::Graphics class: push drawdata into vector and draw in batch like imgui. How does imgui do it?