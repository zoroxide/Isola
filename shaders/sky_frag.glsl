#version 330 core
out vec4 FragColor;
in vec3 vDir;

uniform samplerCube skybox;
uniform bool hasSkybox;
uniform mat3 skyRot;      // world direction -> panorama direction
uniform float exposure;
uniform int skyHDR;       // 1: tone map with ACES, 0: image is already display-ready
uniform float blur;       // mip bias for a soft sky

uniform vec3 sunDir;      // towards the sun (world space)
uniform vec3 sunColor;

// Below the horizon, show the same colour the terrain fog fades into (hides the
// panorama's floor where the terrain/ocean ends at the far plane)
uniform bool horizonFill;
uniform vec3 fogColor;     // used with the procedural sky
uniform float maxLod;

// Camera under the ocean surface
uniform int underwater;
uniform vec3 uwColor;

// Cloud controls
uniform float time;            // seconds
uniform bool cloudEnabled;
uniform float cloudSpeed;
uniform float cloudScale;
uniform float cloudOpacity;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
// Linear radiance -> display colour (same function is used by the terrain shader)
vec3 toDisplay(vec3 c) {
    c *= exposure;
    if (skyHDR == 1) c = aces(c);
    return pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
}

// --- Noise ---
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 6; ++i) { v += a * noise(p); p = rot * p * 2.02; a *= 0.5; }
    return v;
}

// Built-in sky used when no panorama is loaded (display space). Its horizon is the
// terrain fog colour, so distant terrain always melts into the sky.
vec3 proceduralSky(vec3 dir) {
    vec3 sun = normalize(sunDir);
    float y = max(dir.y, 0.0);
    vec3 zenith = vec3(0.22, 0.43, 0.80);
    vec3 col = mix(fogColor, zenith, pow(smoothstep(0.0, 0.75, y), 0.65));
    // Warm glow along the horizon when the sun is low
    float toward = max(dot(dir, sun), 0.0);
    float low = 1.0 - smoothstep(0.0, 0.4, sun.y);
    col = mix(col, vec3(1.0, 0.64, 0.40), low * pow(1.0 - y, 5.0) * toward * toward * 0.6);
    // Sun disc, halo and broad glow
    col += sunColor * (pow(toward, 1500.0) * 4.0 + pow(toward, 60.0) * 0.25 + pow(toward, 6.0) * 0.08);
    if (dir.y < 0.0) col = horizonFill ? fogColor : mix(fogColor, vec3(0.32, 0.34, 0.36), smoothstep(0.0, -0.08, dir.y));
    return clamp(col, 0.0, 1.0);
}

void main() {
    vec3 dir = normalize(vDir);

    vec3 color;
    if (hasSkybox) {
        vec3 d = skyRot * dir;
        vec3 c = blur > 0.0 ? textureLod(skybox, d, blur).rgb : texture(skybox, d).rgb;
        color = toDisplay(c);
        if (horizonFill) {
            // Must match skyColor()/applyFog() in fragment.glsl
            vec3 hd = skyRot * normalize(vec3(dir.x, 0.04, dir.z));
            vec3 horizon = toDisplay(textureLod(skybox, hd, max(maxLod - 4.0, 0.0)).rgb);
            color = mix(color, horizon, smoothstep(0.01, -0.01, dir.y));
        }
    } else {
        color = proceduralSky(dir);
    }

    // --- Clouds: projected onto a flat layer overhead (no seam, no pinching at the zenith) ---
    if (cloudEnabled && dir.y > 0.0) {
        vec2 p = dir.xz / (dir.y + 0.12) * (0.6 * cloudScale);
        vec2 wind = vec2(1.0, 0.3) * time * cloudSpeed * 4.0;
        float n = fbm(p + wind);
        float detail = fbm(p * 3.0 - wind * 1.7);
        float density = smoothstep(0.45, 0.75, n + (detail - 0.5) * 0.25);

        vec3 sun = normalize(sunDir);
        // Cheap lighting: sample density towards the sun for self-shadowing
        float towardSun = fbm(p + wind + sun.xz * 0.08);
        float shade = clamp(1.0 - (towardSun - n) * 3.0, 0.45, 1.0);
        vec3 cloudCol = mix(vec3(0.55, 0.60, 0.68), vec3(1.0, 0.98, 0.95) * mix(vec3(1.0), sunColor, 0.4), shade);
        // Silver lining near the sun
        cloudCol += sunColor * pow(max(dot(dir, sun), 0.0), 12.0) * 0.5 * (1.0 - density);

        float fade = smoothstep(0.0, 0.18, dir.y);
        color = mix(color, cloudCol, density * cloudOpacity * fade);
    }

    if (underwater == 1) color = uwColor * mix(0.6, 1.25, clamp(dir.y * 0.5 + 0.5, 0.0, 1.0));

    FragColor = vec4(color, 1.0);
}
