#pragma once

#include <Hd/Graphics2D.h>

namespace logic {
namespace editor {

class item {
public:
    virtual void draw(glm::mat4& vp, Hd::Graphics2D& p) = 0;

    void move(glm::vec2 d);

    void set_position(glm::vec2 p);
    glm::ivec2 get_position();

    void set_box(glm::vec2 b);
    glm::vec2 get_box();

    bool try_select(glm::vec2 mpos);

    bool is_inside_rect(glm::vec2 p1, glm::vec2 p2);

    void set_snap(bool s);
    bool get_snap();

    virtual void property_editor();

    // uint64_t id = 0;

private:
    glm::vec2 box = {0, 0};
    glm::ivec2 position = {0, 0};
    bool snap = true;
    // int type = 0;

    // bool selected = false;
    // bool selectable = false;

    // TODO: virtual destructor?
};

inline void item::move(glm::vec2 d)
{
    float min = snap ? 10.0f : 1.0f;
    position = glm::round((glm::vec2{position} + d) / min) * min;
}

inline void item::set_position(glm::vec2 p)
{
    float min = snap ? 10.0f : 1.0f;
    position = glm::round(glm::vec2{p} / min) * min;
}

inline glm::ivec2 item::get_position() { return position; }

inline void item::set_box(glm::vec2 b) { box = b; }

inline glm::vec2 item::get_box() { return box; }

inline void item::set_snap(bool s) { snap = s; }

inline bool item::get_snap() { return snap; }

}
}
