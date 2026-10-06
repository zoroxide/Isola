#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 3) in vec2 aUV;
uniform mat4 lightViewProj;
out vec2 UV;

void main() {
    UV = aUV;
    gl_Position = lightViewProj * vec4(aPos, 1.0);
}
