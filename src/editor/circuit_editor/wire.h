#pragma once

#include "../item.h"

namespace logic::editor::circuit_editor {

class wire_point : public item {
public:
    void draw(glm::mat4& vp, Hd::Graphics2D& p) override;
};

class wire {
public:
    wire(wire_point* p1, wire_point* p2);
    wire(const wire&) = delete;
    wire& operator=(const wire&) = delete;

    void draw(glm::mat4& vp, Hd::Graphics2D& p);

    wire_point* get_p1();
    wire_point* get_p2();

private:
    // does not own, assumed always valid
    wire_point* p1 = 0;
    wire_point* p2 = 0;
};

inline wire_point* wire::get_p1() { return p1; }
inline wire_point* wire::get_p2() { return p2; }

}