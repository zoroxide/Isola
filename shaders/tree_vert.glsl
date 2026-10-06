#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aLayer;   // 0 bark, 1 conifer, 2 broadleaf, 3 acacia
layout (location = 4) in vec2 aWind;     // x: trunk bend weight (0 at ground), y: leaf flutter
layout (location = 5) in float aAO;
layout (location = 6) in vec4 iPosYaw;   // instance position + rotation
layout (location = 7) in vec4 iParams;   // scale, phase, tint, LOD fade (0 mesh .. 1 billboard)

uniform mat4 viewProj;
uniform float time;
uniform vec2 windDir;
uniform float windStrength;

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;
flat out float Layer;
out float AO;
out float Tint;
flat out float Fade;
out vec3 LocalNormal;   // unrotated normal (impostor baking)

void main() {
    float s = iParams.x, phase = iParams.y;
    float c = cos(iPosYaw.w), sn = sin(iPosYaw.w);
    mat2 rot = mat2(c, sn, -sn, c);
    vec3 p = aPos * s;
    p.xz = rot * p.xz;
    vec3 n = aNormal;
    n.xz = rot * n.xz;

    // Whole-tree sway: slow gusts rolling across the island plus a quicker secondary motion.
    // Bend weight grows with height^2 so the trunk base stays planted.
    float gust = 0.55 + 0.45 * sin(time * 0.6 + dot(iPosYaw.xz, windDir) * 0.015);
    float sway = (0.55 + 0.35 * sin(time * 1.1 + phase) + 0.15 * sin(time * 2.3 + phase * 1.7)) * gust;
    p.xz += windDir * sway * windStrength * aWind.x * 0.9 * s;
    p.y -= abs(sway) * windStrength * aWind.x * 0.15 * s;

    // Leaf flutter: fast, small, different for every card
    float f = sin(time * 6.5 + dot(aPos, vec3(2.3, 1.7, 3.1)) + phase * 5.0)
            + 0.5 * sin(time * 11.0 + dot(aPos, vec3(-1.3, 2.9, 0.7)));
    p += (n * 0.05 + vec3(windDir.x, 0.02, windDir.y) * 0.04) * f * windStrength * aWind.y * gust;

    FragPos = iPosYaw.xyz + p;
    Normal = n;
    UV = aUV;
    Layer = aLayer;
    AO = aAO;
    Tint = iParams.z;
    Fade = iParams.w;
    LocalNormal = aNormal;
    gl_Position = viewProj * vec4(FragPos, 1.0);
}
