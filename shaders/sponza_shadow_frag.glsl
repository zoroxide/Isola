#version 330 core
// Depth only; alpha-tested leaves and chains cast their cut-out shadows
in vec2 UV;
uniform sampler2D albedoTex;
uniform int alphaMask;
uniform float alphaCutoff;

void main() {
    if (alphaMask == 1 && texture(albedoTex, UV).a < alphaCutoff) discard;
}
