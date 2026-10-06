#version 330 core
// Sponza geometry is stored in world space
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec4 aTangent;   // xyz, w = bitangent sign
layout (location = 3) in vec2 aUV;

uniform mat4 viewProj;

out vec3 FragPos;
out vec3 Normal;
out vec4 Tangent;
out vec2 UV;

void main() {
    FragPos = aPos;
    Normal = aNormal;
    Tangent = aTangent;
    UV = aUV;
    gl_Position = viewProj * vec4(aPos, 1.0);
}
