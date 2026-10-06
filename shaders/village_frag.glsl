#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec3 Tangent;
in vec2 UV;
in vec3 Tint;
in float AO;
in float Emissive;
flat in int Material;

#include "common.glsl"

uniform sampler2DArray albedoMaps;   // plaster, stone, roof, interior, floor, wood, painted, fabric
uniform sampler2DArray normalMaps;
uniform sampler2DArray foliageTex;   // leaf clusters for flower boxes
uniform sampler2D waterDetail;
uniform bool hasAlbedo;
uniform bool hasNormal;
uniform int glassPass;
// This house's own interior lights (lamps, fireplace)
uniform int numHouseLights;
uniform vec4 houseLightPos[4];
uniform vec4 houseLightColor[4];

// Material ids (see Village.cpp)
const int PLASTER = 0, STONE = 1, ROOF = 2, INTERIOR = 3, FLOOR = 4, WOOD = 5, PAINTED = 6, FABRIC = 7;
const int EMISSIVE = 8, WATER = 9, LEAVES = 10, FIRE = 11, METAL = 12, GLASS = 13;

float hash12(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec3 light = normalize(-lightDir);
    vec3 viewVec = viewPos - FragPos;
    float dist = length(viewVec);
    vec3 v = viewVec / dist;
    vec3 n = normalize(Normal);
    if (!gl_FrontFacing) n = -n;

    // --- Window glass (blended pass): sky reflection with Fresnel, a faint tint, warm light behind ---
    if (Material == GLASS) {
        if (glassPass == 0) discard;
        float F = 0.06 + 0.94 * pow(1.0 - abs(dot(n, v)), 5.0);
        vec3 refl = skyColor(reflect(-v, n * sign(dot(n, v))), 1.5);
        vec3 col = mix(vec3(0.12, 0.16, 0.18), refl, 0.6) * (0.4 + 0.6 * sunShadow(FragPos + n * 0.05));
        vec3 h = normalize(light + v);
        col += lightColor * pow(max(dot(n * sign(dot(n, v)), h), 0.0), 200.0) * 2.0 * sunShadow(FragPos);
        FragColor = vec4(applyFog(col, FragPos), clamp(0.18 + F * 0.75, 0.0, 0.92));
        return;
    }
    if (glassPass == 1) discard;

    // --- Self-lit things: lantern glass, candle glow ---
    if (Material == EMISSIVE) {
        FragColor = vec4(applyFog(Tint * Emissive, FragPos), 1.0);
        return;
    }
    // --- Fire: an animated flame shape on crossed cards ---
    if (Material == FIRE) {
        vec2 uv = UV;
        float t = time * 2.2 + hash12(floor(FragPos.xz * 3.0)) * 10.0;
        float wob = (texture(noiseTex, vec2(uv.x * 0.6, uv.y * 0.9 - t * 0.35)).g - 0.5);
        float shape = (1.0 - uv.y) * 1.25 - abs(uv.x - 0.5 + wob * 0.35 * (1.0 - uv.y)) * 2.4;
        shape += (texture(noiseTex, vec2(uv.x * 1.3, uv.y * 1.6 - t * 0.6)).b - 0.5) * 0.5;
        if (shape < 0.05) discard;
        float k = clamp(shape * 1.4, 0.0, 1.0);
        vec3 col = mix(vec3(0.9, 0.18, 0.02), vec3(1.0, 0.72, 0.25), k);
        col = mix(col, vec3(1.0, 0.95, 0.75), smoothstep(0.75, 1.0, k));
        FragColor = vec4(col * (2.2 + 1.6 * k) * Emissive, 1.0);
        return;
    }

    vec3 base;
    float rough = 0.85, specAmt = 0.04;
    if (Material == LEAVES) {
        // Flower box / planter: leaf card with sprinkled blossoms in the tint colour
        vec4 leaf = texture(foliageTex, vec3(UV, 2.0));
        if (leaf.a < 0.5) discard;
        base = leaf.rgb * vec3(0.85, 1.0, 0.75);
        vec2 cell = floor(UV * 14.0);
        vec2 f = fract(UV * 14.0) - 0.5;
        float r = hash12(cell);
        if (r > 0.55 && dot(f, f) < 0.16) base = mix(Tint, vec3(1.0, 0.95, 0.6), step(dot(f, f), 0.012));
        n = normalize(n + vec3(0.0, 0.6, 0.0));
    } else if (Material == WATER) {
        // Fountain water: rippling reflection
        vec4 r1 = texture(waterDetail, FragPos.xz * 0.6 + time * vec2(0.05, 0.03));
        vec4 r2 = texture(waterDetail, FragPos.xz * 1.3 - time * vec2(0.04, 0.06));
        vec3 wn = normalize(vec3((r1.r + r2.r - 1.0) * 0.6, 1.0, (r1.g + r2.g - 1.0) * 0.6));
        float F = 0.03 + 0.97 * pow(1.0 - max(dot(wn, v), 0.0), 5.0);
        vec3 refl = skyColor(reflect(-v, wn), 1.0);
        vec3 col = mix(Tint * (0.35 + 0.4 * max(dot(wn, light), 0.0)), refl, F);
        col += lightColor * pow(max(dot(reflect(-light, wn), v), 0.0), 300.0) * 2.0 * sunShadow(FragPos);
        FragColor = vec4(applyFog(col, FragPos), 1.0);
        return;
    } else if (Material == METAL) {
        base = Tint;
        rough = 0.35; specAmt = 0.35;
    } else {
        int layer = clamp(Material, 0, 7);
        vec3 tex = hasAlbedo ? texture(albedoMaps, vec3(UV, float(layer))).rgb : vec3(0.7);
        if (Material == PAINTED) {
            // painted boards: keep the wood grain, replace the paint colour with the house accent
            float l = dot(tex, vec3(0.3, 0.55, 0.15));
            base = Tint * (0.45 + l * 1.4);
        } else {
            base = tex * Tint;
        }
        if (QUALITY >= 1 && hasNormal) {
            vec2 xy = texture(normalMaps, vec3(UV, float(layer))).rg * 2.0 - 1.0;
            vec3 tn = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
            vec3 T = normalize(Tangent - n * dot(Tangent, n));
            vec3 B = cross(n, T);
            n = normalize(T * tn.x + B * tn.y + n * tn.z);
        }
        if (Material == ROOF) { specAmt = 0.08; rough = 0.6; }
        if (Material == FLOOR || Material == WOOD) { specAmt = 0.06; rough = 0.55; }
    }

    // --- Lighting: sun (with village + terrain shadows), sky ambient, point lights ---
    float shadow = sunShadow(FragPos + n * 0.04);
    float ndl = max(dot(n, light), 0.0);
    vec3 amb = skyAmbient(n) * mix(vec3(0.55, 0.5, 0.45), vec3(1.0), n.y * 0.5 + 0.5) * 0.6 * AO;
    // Outdoor lamps can't reach inside (interior surfaces have low AO); the house's own lights can
    vec3 points = AO > 0.6 ? pointLighting(FragPos, n, numOutdoorLights) : vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (i >= numHouseLights) break;
        vec3 L = houseLightPos[i].xyz - FragPos;
        float r = houseLightPos[i].w, d2 = dot(L, L);
        if (d2 > r * r) continue;
        float f = 1.0 - d2 / (r * r);
        points += houseLightColor[i].rgb * (max(dot(n, L * inversesqrt(max(d2, 1e-4))), 0.0) * 0.85 + 0.15) * f * f / (1.0 + d2 * 0.3);
    }
    vec3 col = (amb + lightColor * ndl * shadow + points) * base;
    vec3 h = normalize(light + v);
    float shine = mix(8.0, 64.0, 1.0 - rough);
    col += lightColor * pow(max(dot(n, h), 0.0), shine) * specAmt * shadow;
    if (Emissive > 0.0) col += base * Emissive;
    FragColor = vec4(applyFog(col, FragPos), 1.0);
}
