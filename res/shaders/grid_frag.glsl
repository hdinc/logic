#version 330 core
#extension GL_ARB_explicit_uniform_location : require

// dots or lines every u_spacing world units, and every 5 * u_spacing, or a
// checkerboard of squares of those sizes. the finer ones fade with u_fine
// while zooming out.

in vec2 v_ndc;
out vec4 fragcolor;

layout(location = 0) uniform mat4 u_inv_vp;
layout(location = 1) uniform vec4 u_color;
layout(location = 2) uniform float u_px; // world units per framebuffer pixel
layout(location = 3) uniform float u_spacing;
layout(location = 4) uniform float u_fine;
layout(location = 5) uniform int u_lines; // 0 dots, 1 lines, 2 checkers
// framebuffer pixels per interface pixel, the marks keep their size on
// hi-dpi screens
layout(location = 6) uniform float u_scale;

// coverage of a mark of the given pixel size at distance d (in pixels)
float mark(float d, float size)
{
    return clamp(size - d + 0.5, 0.0, 1.0);
}

float level(vec2 p, float s, float size)
{
    vec2 d = abs(p - round(p / s) * s) / u_px;
    return u_lines == 1 ? mark(min(d.x, d.y), size * 0.5) : mark(length(d), size);
}

// 1 on every other square of size s
float checker(vec2 p, float s)
{
    vec2 c = floor(p / s);
    return mod(c.x + c.y, 2.0);
}

void main()
{
    vec2 p = (u_inv_vp * vec4(v_ndc, 0, 1)).xy;
    if (u_lines == 2) {
        // faint squares, the small ones fading into the big ones
        float a = mix(checker(p, u_spacing * 5.0), checker(p, u_spacing), u_fine) * 0.12;
        if (a <= 0.0) discard;
        fragcolor = vec4(u_color.rgb, u_color.a * a);
        return;
    }
    float size = 1.1 * u_scale;
    float a = max(level(p, u_spacing, size) * u_fine, level(p, u_spacing * 5.0, size));
    // lines are much more ink than dots
    if (u_lines == 1) a *= 0.45;
    if (a <= 0.0) discard;
    fragcolor = vec4(u_color.rgb, u_color.a * a);
}
