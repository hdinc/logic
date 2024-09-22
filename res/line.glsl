#version 450 core

layout(location = 0) uniform mat4 vp;
layout(location = 1) uniform vec2 p[2];

void main() {
    vec4 p4[2];
    p4[0] = vec4(p[0], 0, 1);
    p4[1] = vec4(p[1], 0, 1);
    gl_Position = vp * p4[gl_VertexID];
}
