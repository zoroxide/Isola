#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 lightViewProj;

void main() {
    gl_Position = lightViewProj * vec4(aPos, 1.0);
}
