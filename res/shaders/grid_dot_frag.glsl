#version 450 core

layout(location=0) uniform mat4 u_mvp = mat4(1.0);

layout(location=1) uniform vec4 u_color = vec4(1,1,1,1);

in vec2 pos;
out vec4 fragcolor;

void main()
{
    vec4 p = inverse(u_mvp) * vec4(pos, 0, 1);
    vec2 g = round(p.xy / 10.0) * 10.0;
    if (length(p.xy - g) < 0.25) {
        fragcolor = u_color;
    } else {
        fragcolor = vec4(0, 0, 0, 1);
    }
}
