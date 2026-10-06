#version 330 core
// Village geometry is stored in world space
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aNormal;    // packed 10:10:10:2
layout (location = 2) in vec4 aTangent;
layout (location = 3) in vec2 aUV;
layout (location = 4) in vec4 aTint;      // tint / 2 (so colours above 1 fit in a byte)
layout (location = 5) in vec4 aMat;       // material id, ambient occlusion * 255, emissive * 255 / 4

uniform mat4 viewProj;

out vec3 FragPos;
out vec3 Normal;
out vec3 Tangent;
out vec2 UV;
out vec3 Tint;
out float AO;
out float Emissive;
flat out int Material;

void main() {
    FragPos = aPos;
    Normal = aNormal.xyz;
    Tangent = aTangent.xyz;
    UV = aUV;
    Tint = aTint.rgb * 2.0;
    AO = aMat.y / 255.0;
    Emissive = aMat.z * (4.0 / 255.0);
    Material = int(aMat.x + 0.5);
    gl_Position = viewProj * vec4(aPos, 1.0);
}
