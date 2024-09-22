
#pragma once

#include "../draw/draw.h"
#include "../misc.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

class ui_element {
public:
    virtual void draw(drawer& d) = 0;
    virtual ~ui_element() = 0;
    virtual bool try_select(glm::vec2 loc) = 0;
    virtual void move(glm::vec2 l) = 0;
    virtual glm::vec2 get_selection_loc() = 0;
    virtual std::string desc() = 0;
    bool selected;
    glm::vec2 loc {0,0};
};

class anchor : public ui_element {
    void draw(drawer& d) override;
    ~anchor() override;
    bool try_select(glm::vec2 l) override;
    void move(glm::vec2 l) override;
    glm::vec2 get_selection_loc() override;
    std::string desc() override { return string_format("anchor"); }
};

class pin : public ui_element {
    void draw(drawer& d) override;
    ~pin() override;
    bool try_select(glm::vec2 l) override;
    void move(glm::vec2 l) override;
    glm::vec2 get_selection_loc() override;
    std::string desc() override { return string_format("pin %.2f, %.2f", loc.x, loc.y); }
};

class line : public ui_element {

public:
    void draw(drawer& d) override;
    ~line() override;
    bool try_select(glm::vec2 l) override;
    void move(glm::vec2 l) override;
    glm::vec2 get_selection_loc() override;
    std::string desc() override { return string_format("line (%.2f, %.2f) -> (%.2f, %.2f)", p1.x, p1.y, p2.x, p2.y); }

    line();

private:
    glm::vec2 p1, p2;
    bool p1s = false;
    bool p2s = false;
};

class spline : public ui_element {

public:
    void draw(drawer& d) override;
    ~spline() override;
    bool try_select(glm::vec2 l) override;
    void move(glm::vec2 l) override;
    glm::vec2 get_selection_loc() override;
    std::string desc() override { return string_format("spline (%.2f, %.2f) , (%.2f, %2.f) , (%.2f, %.2f)", p1.x, p1.y, p2.x, p2.y, p3.x, p3.y); }

    spline();

private:

    void recalc_path();

    glm::vec2 p1, p2, p3;
    bool p1s = false;
    bool p2s = false;
    bool p3s = false;
    std::vector<glm::vec2> path;
};
