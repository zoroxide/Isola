#version 330 core
// Attribute-less grass. Each instance is a clump on a world-aligned grid inside one tile; each clump
// has several thin curved blades (7 vertices: 3 segments + tip). Some clumps carry a wildflower.
uniform mat4 viewProj;
uniform vec2 tileOrigin;
uniform float spacing;
uniform int cells;
uniform int activeBlades;
uniform int bladeVerts;
uniform int grassLights;     // street lamps on the grass (only when it is dark enough to matter)      // 7 (3 segments + tip) near, 5 (2 segments + tip) far
uniform float radius;        // overall grass distance
uniform float innerRadius;   // this ring starts here...
uniform float ringRadius;    // ...and ends here
uniform float widthScale;
uniform float bladeHeight;
uniform vec2 windDir;
uniform float windStrength;
uniform int flowers;

uniform sampler2D heightTex;
uniform float terrainHalf;
uniform float grassBottom;
uniform float rockSlope;
uniform float snowLine;

uniform sampler2D canopyShade;
uniform int hasCanopyShade;
uniform float canopyHalf;

#include "common.glsl"

uniform sampler2DArray matAlbedo;   // terrain materials; layer 0 = grass
uniform float texScale;
uniform vec3 grassTint;

out vec3 FragPos;
out vec3 BaseColor;     // meadow green matched to the ground texture (per vertex: cheap)
out vec3 Ambient;
out vec4 Fog;           // rgb colour, a amount
out vec3 Normal;
out float T;            // 0 at the root, 1 at the tip
out float AO;
out float Shadow;
out vec3 Variation;     // per-clump colour multiplier
flat out vec3 FlowerColor;
out float IsFlower;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float heightAt(vec2 p) { return textureLod(heightTex, p / (2.0 * terrainHalf) + 0.5, 0.0).r; }
void cull() {
    gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
    FragPos = vec3(0); Normal = vec3(0, 1, 0); T = 0.0; AO = 0.0; Shadow = 0.0; Variation = vec3(0); FlowerColor = vec3(0); IsFlower = 0.0;
    BaseColor = vec3(0); Ambient = vec3(0); Fog = vec4(0);
}

void main() {
    int blade = gl_VertexID / bladeVerts, local = gl_VertexID % bladeVerts;
    if (blade >= activeBlades) { cull(); return; }
    ivec2 cell = ivec2(gl_InstanceID % cells, gl_InstanceID / cells);
    vec2 cellPos = tileOrigin + (vec2(cell) + 0.5) * spacing;
    float c1 = hash(cellPos), c2 = hash(cellPos + 17.3), c3 = hash(cellPos + 5.5);
    vec2 clump = cellPos + (vec2(c1, c2) - 0.5) * spacing * 0.9;
    // No grass on village streets, the plaza or under houses
    if (villageMaskCovered(clump)) { cull(); return; }

    float dist = length(clump - viewPos.xz);
    // Rings hand over with a dithered overlap; far clumps thin out
    float keep = 1.0 - smoothstep(radius * 0.55, radius, dist);
    keep *= innerRadius > 0.0 ? smoothstep(innerRadius - 1.5, innerRadius + 1.5, dist)
                              : 1.0 - smoothstep(ringRadius - 1.5, ringRadius + 1.5, dist);
    if (c3 > keep || abs(clump.x) > terrainHalf || abs(clump.y) > terrainHalf) { cull(); return; }

    float h = heightAt(clump);
    float hx = heightAt(clump + vec2(1.0, 0.0)) - heightAt(clump - vec2(1.0, 0.0));
    float hz = heightAt(clump + vec2(0.0, 1.0)) - heightAt(clump - vec2(0.0, 1.0));
    vec3 groundN = normalize(vec3(-hx * 0.5, 1.0, -hz * 0.5));
    float slope = 1.0 - groundN.y;
    // Grow where the terrain shows grass: not on sand, rock or snow; meadow patches vary in height
    vec4 nz = textureLod(noiseTex, clump * (1.0 / 60.0), 0.0);
    float grow = smoothstep(grassBottom, grassBottom + 1.5, h)
               * (1.0 - smoothstep(rockSlope - 0.12, rockSlope - 0.02, slope))
               * (1.0 - smoothstep(snowLine - 12.0, snowLine - 4.0, h));
    grow *= 0.35 + 0.9 * nz.r;
    if (grow < 0.12) { cull(); return; }

    // Per-blade randomness
    vec2 bs = clump + vec2(float(blade) * 7.13, float(blade) * 3.71);
    float r1 = hash(bs), r2 = hash(bs + 1.7), r3 = hash(bs + 9.1), r4 = hash(bs + 4.4);
    float ang = r1 * 6.2831853;
    vec2 root = clump + vec2(cos(ang), sin(ang)) * (0.03 + 0.1 * r2) * widthScale;
    // Blades lean outwards from the clump centre, with a random twist
    float yaw = ang + (r3 - 0.5) * 1.6;
    vec2 facing = vec2(cos(yaw), sin(yaw));
    vec2 side = vec2(-facing.y, facing.x);

    bool flower = flowers == 1 && blade == 0 && hash(clump + 31.7) < 0.025 && bladeVerts == 7;
    // Far blades are a bit shorter so the grass edge melts into the ground
    float height = bladeHeight * (0.55 + 0.65 * r4) * grow * (flower ? 1.15 : 1.0) * (1.0 - 0.35 * smoothstep(radius * 0.4, radius, dist));
    int segs = (bladeVerts - 1) / 2;
    bool tipVert = local == bladeVerts - 1;
    float t = tipVert ? 1.0 : float(local / 2) / float(segs);
    float sideSign = tipVert ? 0.0 : (float(local % 2) - 0.5);
    // Blade shape: widest near the base, tapering to a point; a bit wider far away so it still
    // covers the ground (a pixel-wide blade at 40 m would just shimmer)
    float width = (0.02 + 0.016 * r2) * widthScale * (1.0 + dist * 0.02) * (1.0 - t * t * 0.92);
    float flowerHead = 0.0;
    if (flower) {
        // Thin stem up to 80%, then a camera-facing petal diamond
        if (local >= 4) { t = local == 6 ? 1.0 : 0.9; width = local == 6 ? 0.0 : 0.075; flowerHead = 1.0; }
        else if (local >= 2) { t = 0.8; width = 0.012; }
        else width = 0.008;
    }

    // Bend: natural lean + rolling gusts + flutter + parting around the player
    float gust = textureLod(noiseTex, clump * (1.0 / 25.0) - windDir * time * 0.12, 0.0).g;
    float wave = 0.5 + 0.5 * sin(dot(clump, windDir) * 0.25 - time * 2.1 + gust * 5.0);
    vec2 bend = facing * (0.3 + 0.7 * r3 * r3)
              + windDir * windStrength * (0.2 + 1.0 * wave * gust * 1.4)
              + side * sin(time * 4.5 + r1 * 20.0) * 0.08 * windStrength;
    vec2 away = root - viewPos.xz;
    float pd = length(away);
    if (pd < 1.3 && abs(viewPos.y - 1.7 - h) < 1.5) bend += away / max(pd, 1e-3) * (1.0 - smoothstep(0.15, 1.3, pd)) * 1.6;
    float bl = length(bend);
    if (bl > 1.3) bend *= 1.3 / bl;
    // Curved blade: horizontal offset grows with t^2, height shrinks as it bends over
    vec2 off = bend * height * t * t * 0.9;
    float up = height * t * (1.0 - 0.3 * min(bl * bl, 1.3) * t);
    vec3 sideDir = vec3(side.x, 0.0, side.y);
    if (flowerHead > 0.5) {            // petals face the camera
        vec3 toCam = normalize(viewPos - vec3(root.x, h + up, root.y));
        sideDir = normalize(cross(vec3(0.0, 1.0, 0.0), toCam) + vec3(1e-4, 0.0, 0.0));
    }
    vec3 pos = vec3(root.x, h - 0.03, root.y) + sideDir * width * sideSign + vec3(off.x, up, off.y);
    if (flower && local == 6) pos.y -= 0.035;   // diamond: tip folds back towards the centre

    // Normal: across the blade face, rounded across its width, blended with the ground normal
    vec3 tangent = normalize(vec3(bend.x * 1.8 * t, 1.0, bend.y * 1.8 * t));
    vec3 faceN = normalize(cross(vec3(side.x, 0.0, side.y), tangent));
    faceN = normalize(faceN + vec3(side.x, 0.0, side.y) * sideSign * 1.2);
    Normal = normalize(mix(faceN, groundN, 0.55));

    // Lighting inputs computed per vertex (cheap): sun shadow and canopy shade
    float sh = 1.0;
    if (hasSunShadow == 1) {
        float need = textureLod(sunShadowTex, pos.xz / (2.0 * shadowHalf) + 0.5, 0.0).r;
        sh = mix(1.0, smoothstep(need - 0.3, need + 1.0, pos.y), shadowStrength);
    }
    sh = min(sh, villageSunShadowFast(pos + vec3(0.0, 0.05, 0.0)));   // houses shade the lawns
    float canopy = hasCanopyShade == 1 ? textureLod(canopyShade, pos.xz / (2.0 * canopyHalf) + 0.5, 0.0).r : 0.0;

    // Colour variation: lush, deep and dry clumps, following the meadow noise
    vec3 lush = vec3(0.95, 1.1, 0.75), deep = vec3(0.75, 0.92, 0.7), dry = vec3(1.25, 1.1, 0.62);
    vec3 varc = mix(mix(deep, lush, nz.b), dry, smoothstep(0.55, 0.85, nz.a) * 0.8);
    Variation = varc * (0.85 + 0.3 * r2);
    const vec3 palette[4] = vec3[](vec3(1.0, 0.95, 0.9), vec3(1.0, 0.85, 0.2), vec3(0.6, 0.4, 0.95), vec3(0.95, 0.35, 0.3));
    FlowerColor = palette[int(hash(clump + 2.2) * 3.99)];
    IsFlower = flowerHead;

    // Per-vertex shading inputs: ground colour, sky light, fog
    vec3 ground = textureLod(matAlbedo, vec3(root * (0.16 * texScale), 0.0), 6.0).rgb * grassTint;
    BaseColor = mix(vec3(0.20, 0.34, 0.08), ground * 1.1, 0.4);
    Ambient = skyAmbient(vec3(0.0, 1.0, 0.0)) * 0.6;
    if (grassLights > 0) Ambient += pointLighting(pos, vec3(0.0, 1.0, 0.0), grassLights);
    if (underwater == 1) Fog = vec4(uwColor, 1.0 - exp(-length(pos - viewPos) * 0.035));
    else Fog = vec4(fogBaseColor(normalize(pos - viewPos)), fogAmount(pos));
    FragPos = pos;
    T = t;
    AO = mix(0.55, 1.0, pow(t, 0.6)) * (1.0 - 0.5 * canopy);
    Shadow = sh;
    gl_Position = viewProj * vec4(pos, 1.0);
}
