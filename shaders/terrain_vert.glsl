#version 330 core
// Chunked LOD terrain: patch vertices hold local grid coordinates, heights come from the height map
layout (location = 0) in vec3 aGrid;    // x, z in grid cells relative to the chunk; z = 1 for skirt vertices
layout (location = 1) in vec2 iChunk;   // chunk origin in grid cells (per instance)

uniform mat4 viewProj;
uniform sampler2D heightTex;
uniform int gridN;
uniform float gridScale;
uniform float halfExtent;
uniform float skirtDepth;

out vec3 FragPos;

void main() {
    ivec2 g = clamp(ivec2(iChunk + aGrid.xy), ivec2(0), ivec2(gridN - 1));
    float h = texelFetch(heightTex, g, 0).r - aGrid.z * skirtDepth;
    FragPos = vec3(float(g.x) * gridScale - halfExtent, h, float(g.y) * gridScale - halfExtent);
    gl_Position = viewProj * vec4(FragPos, 1.0);
}
