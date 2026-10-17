#version 450 core

layout(location=0) uniform mat4 u_mvp = mat4(1.0);

layout(location=0) in vec2 p;

void main()
{
    gl_Position = u_mvp * vec4(p, 0, 1);
}
