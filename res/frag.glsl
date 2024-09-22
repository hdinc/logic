#version 450 core

layout(location = 5) uniform vec4 color = vec4(1, 1, 1, 1);

out vec4 c;

void main() {
    c = color;
}
