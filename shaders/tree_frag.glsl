#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
flat in float Layer;
in float AO;
in float Tint;
flat in float Fade;

#include "common.glsl"

uniform sampler2DArray foliageTex;

// Interleaved gradient noise: stable per-pixel dither for the LOD cross-fade
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    if (Fade > 0.0 && ign(gl_FragCoord.xy) < Fade) discard;
    vec4 tex = texture(foliageTex, vec3(UV, Layer));
    bool leaf = Layer > 0.5;
    if (leaf) {
        // Alpha test that keeps its coverage in the distance (mip averaging would thin the crowns)
        vec2 dx = dFdx(UV * 1024.0), dy = dFdy(UV * 1024.0);
        float lod = max(0.0, 0.5 * log2(max(dot(dx, dx), dot(dy, dy))));
        if (tex.a * (1.0 + lod * 0.3) < 0.5) discard;
    }
    vec3 light = normalize(-lightDir);
    vec3 v = normalize(viewPos - FragPos);
    vec3 n = normalize(Normal);
    vec3 albedo = tex.rgb * Tint;
    if (!leaf) albedo *= vec3(0.9, 0.85, 0.8);

    float shadow = sunShadow(FragPos);
    vec3 amb = skyAmbient(n) * mix(vec3(0.5, 0.48, 0.38), vec3(1.0), n.y * 0.5 + 0.5) * 0.6;
    float ndl = dot(n, light);
    float diff = leaf ? clamp(ndl * 0.6 + 0.4, 0.0, 1.0) : max(ndl, 0.0);
    vec3 col = (amb + lightColor * diff * 0.85 * shadow + pointLighting(FragPos, n, numOutdoorLights)) * albedo * AO;
    if (leaf) {
        // Sunlight shining through the leaves when looking towards the sun
        float trans = pow(max(dot(-v, light), 0.0), 4.0);
        col += lightColor * albedo * vec3(0.9, 1.0, 0.5) * trans * 0.6 * AO * shadow;
    }
    FragColor = vec4(applyFog(col, FragPos), 1.0);
}
