#version 450 core

layout(location=1) uniform vec4 u_color = vec4(1,1,1,1);

out vec4 fragcolor;

void main()
{
    fragcolor = u_color;
}
