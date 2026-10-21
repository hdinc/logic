#version 330 core
#extension GL_ARB_explicit_uniform_location : require

out vec2 v_ndc;

void main()
{
    vec2 p[4] = vec2[4](vec2(-1, -1), vec2(1, -1), vec2(-1, 1), vec2(1, 1));
    v_ndc = p[gl_VertexID];
    gl_Position = vec4(v_ndc, 0, 1);
}
