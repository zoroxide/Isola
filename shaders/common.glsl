// ---------------------------------------------------------------------------
// Shared lighting / atmosphere code, included by the scene shaders.
// QUALITY (set by the engine from the GPU's tier): 0 potato, 1 low, 2 medium, 3 high.
#ifndef QUALITY
#define QUALITY 2
#endif
// Texture units: 3 sky cube, 8 noise, 10 sun shadow height map.
// ---------------------------------------------------------------------------
uniform vec3 lightDir;        // direction the sunlight travels
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform float time;

uniform vec3 fogColor;
uniform float fogDensity;     // distance haze
uniform float fogHeightDensity;
uniform float fogHeightFalloff;
uniform float fogBaseY;       // height fog is thickest below this altitude
uniform float sunGlow;        // how much the haze lights up towards the sun

uniform int underwater;
uniform vec3 uwColor;

uniform samplerCube skyTex;
uniform int hasSky;
uniform mat3 skyRot;
uniform float skyExposure;
uniform int skyHDR;
uniform float skyMaxLod;
uniform int fogFromSky;

uniform sampler2D sunShadowTex;   // per texel: height above which a point sees the sun
uniform int hasSunShadow;
uniform float shadowHalf;         // half size of the area it covers (world units)
uniform float shadowStrength;

// Local geometry shadows: roofs, wall openings and furniture cannot be represented
// by the island's height-field shadow map. Unit 13 holds their directional depth map.
uniform sampler2DShadow villageShadowTex;
uniform mat4 villageLightViewProj;
uniform int hasVillageShadow;
uniform float villageShadowStrength;

uniform sampler2D noiseTex;       // tileable noise, 4 octaves in r,g,b,a

// Point lights (village lamps, lanterns, fires): the nearest ones to the camera, outdoor lights first.
// Terrain / grass / trees only use the outdoor ones; buildings use all of them.
#define MAX_POINT_LIGHTS 16
uniform int numPointLights;
uniform int numOutdoorLights;
uniform vec4 pointLightPos[MAX_POINT_LIGHTS];     // xyz, radius
uniform vec4 pointLightColor[MAX_POINT_LIGHTS];   // rgb (intensity included)

// Village ground mask (unit 14): r = cobblestone paving, g = no grass (paving, house footprints)
uniform sampler2D villageMask;
uniform int hasVillageMask;
uniform vec4 villageMaskRect;     // min x, min z, 1/size x, 1/size z

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Sky radiance in direction d as display colour (same transform as the sky shader)
vec3 skyColor(vec3 d, float lod) {
    vec3 c = textureLod(skyTex, skyRot * d, lod).rgb * skyExposure;
    if (skyHDR == 1) c = aces(c);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

// Diffuse light from the sky hemisphere around normal n
vec3 skyAmbient(vec3 n) {
    if (hasSky == 1) return skyColor(normalize(n + vec3(0.0, 0.6, 0.0)), max(skyMaxLod - 1.0, 0.0)) * 0.9;
    return mix(vec3(0.30, 0.28, 0.24), vec3(0.50, 0.62, 0.80), n.y * 0.5 + 0.5);
}

// Cheap noise from the tileable noise texture (0..1). One fetch instead of dozens of sin() calls.
float noiseMacro(vec2 p) { return texture(noiseTex, p).r; }
vec4 noise4(vec2 p) { return texture(noiseTex, p); }

// Sun visibility (1 lit, 0 in shadow) for a world-space point, with a soft penumbra that grows
// with the height of the blocker above the point
float terrainSunShadow(vec3 p) {
    if (hasSunShadow == 0) return 1.0;
    vec2 uv = p.xz / (2.0 * shadowHalf) + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float need = texture(sunShadowTex, uv).r;
    float s = smoothstep(need - 0.3, need + 1.2 + 0.08 * max(need - p.y, 0.0), p.y);
    return mix(1.0, s, shadowStrength);
}

float villageSunShadowFast(vec3 p);
float villageSunShadow(vec3 p) {
#if QUALITY <= 1
    return villageSunShadowFast(p);
#endif
    if (hasVillageShadow == 0) return 1.0;
    vec3 q = (villageLightViewProj * vec4(p, 1.0)).xyz * 0.5 + 0.5;
    if (any(lessThanEqual(q, vec3(0.0))) || any(greaterThanEqual(q, vec3(1.0)))) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(villageShadowTex, 0));
    // Small receiver bias plus hardware-filtered 3x3 PCF: stable soft edges,
    // including thin frames and furniture, without detaching wall shadows.
    // 4 hardware-filtered taps (each already a 2x2 comparison): soft edges at a third of the cost
    float visibility = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2((i & 1) == 0 ? -0.5 : 0.5, (i & 2) == 0 ? -0.5 : 0.5) * texel * 1.5;
        visibility += texture(villageShadowTex, vec3(q.xy + o, q.z - 0.00018));
    }
    return mix(1.0, visibility * 0.25, villageShadowStrength);
}

// One hardware-filtered tap: for per-vertex use (grass)
float villageSunShadowFast(vec3 p) {
    if (hasVillageShadow == 0) return 1.0;
    vec3 q = (villageLightViewProj * vec4(p, 1.0)).xyz * 0.5 + 0.5;
    if (any(lessThanEqual(q, vec3(0.0))) || any(greaterThanEqual(q, vec3(1.0)))) return 1.0;
    return mix(1.0, texture(villageShadowTex, vec3(q.xy, q.z - 0.0003)), villageShadowStrength);
}

float sunShadow(vec3 p) {
    return min(terrainSunShadow(p), villageSunShadow(p));
}

vec3 pointLighting(vec3 p, vec3 n, int count) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= count) break;
        vec3 L = pointLightPos[i].xyz - p;
        float r = pointLightPos[i].w;
        float d2 = dot(L, L);
        if (d2 > r * r) continue;
        float d = sqrt(d2);
        // Smooth window to zero at the radius, inverse-square-ish in between
        float f = 1.0 - d2 / (r * r);
        float ndl = max(dot(n, L / max(d, 1e-3)), 0.0) * 0.85 + 0.15 * step(d, 0.9);
        sum += pointLightColor[i].rgb * ndl * f * f / (1.0 + d2 * 0.3);
    }
    return sum;
}

vec2 villageMaskAt(vec2 xz) {
    if (hasVillageMask == 0) return vec2(0.0);
    vec2 uv = (xz - villageMaskRect.xy) * villageMaskRect.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec2(0.0);
    return textureLod(villageMask, uv, 0.0).rg;
}
bool villageMaskCovered(vec2 xz) { return villageMaskAt(xz).g > 0.35; }

vec3 fogBaseColor(vec3 rd) {
    vec3 fc = fogColor;
#if QUALITY >= 1
    if (hasSky == 1 && fogFromSky == 1)
        fc = skyColor(normalize(vec3(rd.x, 0.04 + max(rd.y, 0.0) * 0.5, rd.z)), max(skyMaxLod - 4.0, 0.0));
#endif
    // Haze glows around the sun (forward scattering)
    vec3 sunDir = normalize(-lightDir);
    fc += lightColor * pow(max(dot(rd, sunDir), 0.0), 8.0) * sunGlow;
    return fc;
}

// Distance haze + height fog (thicker in valleys and over the sea), or underwater murk
vec3 applyFog(vec3 color, vec3 worldPos) {
    vec3 ray = worldPos - viewPos;
    float dist = length(ray);
    vec3 rd = ray / max(dist, 1e-4);
    if (underwater == 1) {
        float f = 1.0 - exp(-dist * 0.035);
        return mix(color * vec3(0.6, 0.9, 1.0), uwColor, f);
    }
    float fd = 1.0 - exp(-pow(fogDensity * dist, 2.0));
    // Analytic integral of density * exp(-falloff * (y - base)) along the view ray
    float h0 = max(viewPos.y - fogBaseY, -20.0);
    float k = fogHeightFalloff * rd.y * dist;
    float integral = abs(k) > 1e-3 ? (1.0 - exp(-k)) / k : 1.0;
    float fh = 1.0 - exp(-fogHeightDensity * exp(-fogHeightFalloff * h0) * dist * integral);
    float f = clamp(1.0 - (1.0 - fd) * (1.0 - fh), 0.0, 1.0);
    return mix(color, fogBaseColor(rd), f);
}

// Just the fog amount (for transparent surfaces that also need alpha)
float fogAmount(vec3 worldPos) {
    vec3 ray = worldPos - viewPos;
    float dist = length(ray);
    vec3 rd = ray / max(dist, 1e-4);
    float fd = 1.0 - exp(-pow(fogDensity * dist, 2.0));
    float h0 = max(viewPos.y - fogBaseY, -20.0);
    float k = fogHeightFalloff * rd.y * dist;
    float integral = abs(k) > 1e-3 ? (1.0 - exp(-k)) / k : 1.0;
    float fh = 1.0 - exp(-fogHeightDensity * exp(-fogHeightFalloff * h0) * dist * integral);
    return clamp(1.0 - (1.0 - fd) * (1.0 - fh), 0.0, 1.0);
}
