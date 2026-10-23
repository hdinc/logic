#version 330 core
#extension GL_ARB_explicit_uniform_location : require

// filled triangles of symbols, in world coordinates

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
out vec4 v_color;

layout(location = 0) uniform mat4 u_vp;

void main()
{
    v_color = a_color;
    gl_Position = u_vp * vec4(a_pos, 0, 1);
}
