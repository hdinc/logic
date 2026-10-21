#version 330 core
#extension GL_ARB_explicit_uniform_location : require

// dots or lines every u_spacing world units, and every 5 * u_spacing. the
// finer ones fade with u_fine while zooming out.

in vec2 v_ndc;
out vec4 fragcolor;

layout(location = 0) uniform mat4 u_inv_vp;
layout(location = 1) uniform vec4 u_color;
layout(location = 2) uniform float u_px; // world units per pixel
layout(location = 3) uniform float u_spacing;
layout(location = 4) uniform float u_fine;
layout(location = 5) uniform int u_lines;

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

void main()
{
    vec2 p = (u_inv_vp * vec4(v_ndc, 0, 1)).xy;
    float a = max(level(p, u_spacing, 1.1) * u_fine, level(p, u_spacing * 5.0, 1.1));
    // lines are much more ink than dots
    if (u_lines == 1) a *= 0.45;
    if (a <= 0.0) discard;
    fragcolor = vec4(u_color.rgb, u_color.a * a);
}
