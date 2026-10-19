#pragma once

#include "../model/workspace.h"
#include "../sim/value.h"

#include <Hd/Renderer2D.h>
#include <Hd/Shader.h>
#include <Hd/VAO.h>

#include <string>
#include <vector>

namespace logic::render {

// colors of the canvas
struct theme {
    glm::vec4 background;
    glm::vec4 grid;
    glm::vec4 symbol;
    glm::vec4 wire;
    glm::vec4 label; // instance names
    glm::vec4 selection;
    glm::vec4 guide;
    glm::vec4 pin;
    glm::vec4 control_point;
    glm::vec4 error;
    // wires connecting ports of different widths
    glm::vec4 width_error;
    glm::vec4 preview;

    // signal values while simulating, like logisim
    glm::vec4 high;
    glm::vec4 low;
    glm::vec4 unknown;
    glm::vec4 floating;
    glm::vec4 bus;
};

const theme& dark_theme();
const theme& light_theme();

// the theme in use
const theme& colors();
void set_theme(const theme& t);

enum class grid_style {
    dots,
    lines
};

enum class align {
    left,
    center,
    right
};

// line widths in world units, the grid is model::grid
constexpr float stroke = 1.0f;
constexpr float wire_width = 1.0f;
constexpr float bus_width = model::bus_stroke;

// owns the gl resources shared by all editors. needs a current gl context.
//
// an editor draws a frame between begin() and end(), everything in between
// is collected and drawn at end().
class renderer {
public:
    renderer();

    renderer(const renderer&) = delete;
    renderer& operator=(const renderer&) = delete;

    // clears to the theme's background, draws the grid and starts
    // collecting shapes
    void begin(const glm::mat4& vp);
    void end();

    // size of a pixel in world units
    float pixel() const { return g.pixel(); }

    void line(glm::vec2 a, glm::vec2 b, glm::vec4 color, float width = stroke) { g.line(a, b, width, color); }
    void circle(glm::vec2 c, float radius, glm::vec4 color) { g.circle(c, radius, color); }
    void ring(glm::vec2 c, float radius, glm::vec4 color, float width = stroke) { g.ring(c, radius, width, color); }
    void rect(glm::vec2 a, glm::vec2 b, glm::vec4 color, float corner = 0.0f) { g.rect(a, b, color, corner); }
    void rect_outline(glm::vec2 a, glm::vec2 b, glm::vec4 color, float width = stroke, float corner = 0.0f) { g.rectOutline(a, b, width, color, corner); }

    // pos is the left end of the baseline for align::left
    void text(const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a = align::left);
    // like text() but pos.y is the vertical middle of capitals and digits
    void text_middle(const std::string& str, glm::vec2 pos, float size, glm::vec4 color, align a = align::left);

    bool show_grid = true;
    grid_style grid = grid_style::dots;

    // multiplies the line widths of symbol shapes, for small previews
    float stroke_scale = 1.0f;

    Hd::Renderer2D g;

private:
    void draw_grid(const glm::mat4& vp);

    Hd::Shader grid_shader;
    Hd::VAO grid_vao;
};

void draw_shape(renderer& r, const model::shape& s, glm::ivec2 pos, model::transform xf, glm::vec4 color);

// shapes of a symbol placed at pos. pins get a dot unless pin_connected
// says they are connected (null: none is).
void draw_symbol(renderer& r, const model::symbol& s, glm::ivec2 pos, model::transform xf, glm::vec4 color,
    const std::vector<bool>* pin_connected = nullptr);

// instance of a cell in a schematic, missing cells are drawn as a red box.
// pin_nets: net of each port of the instance (-1 if not connected), null if
// none is connected.
void draw_instance(renderer& r, const model::workspace& ws, const model::instance& i, glm::vec4 color,
    const std::vector<int>* pin_nets = nullptr);

// connector of a port inside its cell's schematic. the connection point
// gets a dot like an instance pin if nothing is connected to it.
void draw_port(renderer& r, const model::port& p, glm::vec4 color, bool connected = false);

// color of a wire carrying the value. width 0 is a net without ports.
glm::vec4 value_color(const sim::value& v, int width);

// value of a port while simulating: inside the connector for single bits,
// above it for buses
void draw_port_value(renderer& r, const model::port& p, const sim::value& v);

void draw_wire(renderer& r, const model::wire& w, glm::vec4 color, bool bus = false);

}
