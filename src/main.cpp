#include <stdio.h>
#include <Hd.h>
#include "draw/draw.h"
#include "sim/circuit.h"

int main()
{
    Hd::Window& window = Hd::Window::getInstance();

    Hd::Camera cam;
    cam.setOrthographic();
    cam.enableInput();

    circuit c(cam);

    Hd::Gui gui;

    gui.Functions.add([&c] { c.gui(); });

    while (!window.ShouldClose()) {
        window.PollEvents();

        c.process_inputs();

        glClear(GL_COLOR_BUFFER_BIT);
        c.draw();

        gui.Draw();

        window.SwapBuffers();
    }
}
