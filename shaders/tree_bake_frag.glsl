#version 330 core
// Renders a tree into the impostor atlas: albedo (AO baked in) and tree-space normal
layout (location = 0) out vec4 outAlbedo;
layout (location = 1) out vec4 outNormal;

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
flat in float Layer;
in float AO;
in vec3 LocalNormal;

uniform sampler2DArray foliageTex;

void main() {
    vec4 tex = texture(foliageTex, vec3(UV, Layer));
    bool leaf = Layer > 0.5;
    if (leaf && tex.a < 0.5) discard;
    vec3 albedo = tex.rgb * (leaf ? 1.0 : 0.85) * AO;
    outAlbedo = vec4(albedo, 1.0);
    // Leaves: flag 1 in alpha so the billboard shader adds translucency; bark: 0.5
    outNormal = vec4(normalize(LocalNormal) * 0.5 + 0.5, leaf ? 1.0 : 0.5);
}
