#version 450 core

out vec2 pos;

void main()
{
    vec2 p[4] = {vec2(-1, -1), vec2(-1, 1), vec2(1, -1), vec2(1, 1)};
    pos = p[gl_VertexID];
    gl_Position = vec4(pos, 0, 1);
}
