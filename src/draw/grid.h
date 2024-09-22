#pragma once

#include <Hd.h>
#include "draw.h"

class grid {
public:
    grid(drawer& d);
    void draw();

private:
    drawer& d;
};
