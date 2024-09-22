#version 450 core

layout(location = 0) in vec2 pos;

layout(location = 0) uniform mat4 vp;

void main() {
    gl_Position = vp * vec4(pos, 0, 1);
}
