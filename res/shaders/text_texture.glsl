#version 450 core

in vec2 texcoord;
out vec4 fragcolor;

layout(location=1) uniform sampler2D u_tex;
layout(location=6) uniform vec3 color = vec3(1);

void main()
{
   float alpha = texture(u_tex,texcoord).x;
   fragcolor = vec4(color, alpha);
}
