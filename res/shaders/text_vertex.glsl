#version 450 core

layout(location=0) in vec3 a_position;

layout(location=0) uniform mat4 u_mvp = mat4(1);
layout(location=2) uniform vec2[4] u_texcoord;

out vec2 texcoord;

void main()
{
    gl_Position = u_mvp * vec4(a_position,1);
    texcoord = u_texcoord[gl_VertexID];
}
