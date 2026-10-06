#version 330 core
out vec4 FragColor;
in vec3 FragPos;

#include "common.glsl"

uniform sampler2D heightTex;
uniform int gridN;
uniform float gridScale;
uniform float halfExtent;

uniform sampler2D texture1;         // optional custom grass texture
uniform sampler2DArray matAlbedo;   // 0 grass, 1 grass2, 2 rock, 3 sand, 4 snow
uniform sampler2DArray matNormal;
uniform int hasMaterials;
uniform int customGrass;

uniform float waterY;
uniform float beachWidth;
uniform float rockSlope;
uniform float snowLine;
uniform float snowBlend;
uniform float texScale;
uniform vec3 grassTint;
uniform vec3 sandTint;
uniform vec3 rockTint;
uniform vec3 snowTint;
uniform vec3 waterShallow;
uniform vec3 waterDeep;

uniform sampler2D canopyShade;
uniform int hasCanopyShade;
uniform float canopyHalf;

// Animated caustic web on the sea floor
float caustics(vec2 p, float t) {
    vec2 i = p * 0.35, q = i;
    float c = 1.0;
    for (int n = 0; n < 3; n++) {
        float tt = t * 0.6 * (1.0 - (3.5 / float(n + 1)));
        q = i + vec2(cos(tt - q.x) + sin(tt + q.y), sin(tt - q.y) + cos(tt + q.x));
        c += 1.0 / length(vec2(i.x / (sin(q.x + tt) / 0.005), i.y / (cos(q.y + tt) / 0.005)));
    }
    c = 1.17 - pow(c / 3.0, 1.4);
    return clamp(pow(abs(c), 8.0), 0.0, 1.0);
}

// Normal maps store only X/Y (RGTC2 compressed); rebuild Z
vec3 tangentNormal(vec2 uv, int layer) {
    vec2 xy = texture(matNormal, vec3(uv, layer)).rg * 2.0 - 1.0;
    return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

void sampleTop(int layer, vec2 uv, vec3 N, bool detail, out vec3 albedo, out vec3 n) {
    albedo = texture(matAlbedo, vec3(uv, layer)).rgb;
    if (!detail) { n = N; return; }
    vec3 t = tangentNormal(uv, layer);
    n = normalize(vec3(t.x + N.x, abs(t.z) * N.y, t.y + N.z));
}
void sampleTriplanar(int layer, vec3 p, vec3 N, bool detail, out vec3 albedo, out vec3 n) {
    vec3 w = pow(abs(N), vec3(4.0));
    w /= (w.x + w.y + w.z);
    albedo = texture(matAlbedo, vec3(p.zy, layer)).rgb * w.x
           + texture(matAlbedo, vec3(p.xz, layer)).rgb * w.y
           + texture(matAlbedo, vec3(p.xy, layer)).rgb * w.z;
    if (!detail) { n = N; return; }
    vec3 tx = tangentNormal(p.zy, layer);
    vec3 ty = tangentNormal(p.xz, layer);
    vec3 tz = tangentNormal(p.xy, layer);
    tx = vec3(tx.xy + N.zy, abs(tx.z) * N.x);
    ty = vec3(ty.xy + N.xz, abs(ty.z) * N.y);
    tz = vec3(tz.xy + N.xy, abs(tz.z) * N.z);
    n = normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

void main() {
    vec3 P = FragPos;
    // Deep sea floor: hidden behind opaque deep water when seen from above, skip the work
    if (underwater == 0 && viewPos.y > waterY && P.y < waterY - 14.0) { FragColor = vec4(uwColor, 1.0); return; }
    vec3 viewVec = viewPos - P;
    float dist = length(viewVec);
    vec3 viewDir = viewVec / dist;
    vec3 light = normalize(-lightDir);

    // Full-resolution normal from the height map, independent of the chunk's LOD
    vec2 gc = (P.xz + halfExtent) / gridScale;
    vec2 uv = (gc + 0.5) / float(gridN);
    float t = 1.0 / float(gridN);
    float hL = texture(heightTex, uv - vec2(t, 0.0)).r, hR = texture(heightTex, uv + vec2(t, 0.0)).r;
    float hD = texture(heightTex, uv - vec2(0.0, t)).r, hU = texture(heightTex, uv + vec2(0.0, t)).r;
    vec3 N = normalize(vec3(hL - hR, 2.0 * gridScale, hD - hU));

    float slope = 1.0 - N.y;
    vec4 nz = noise4(P.xz * (1.0 / 260.0));
    float macro = nz.r;
    float mid = noise4(P.xz * (1.0 / 65.0)).g;
    // normal maps only up close (texture bandwidth is the main cost)
#if QUALITY == 0
    bool detail = false;
#elif QUALITY >= 3
    bool detail = dist < 110.0;
#else
    bool detail = dist < 70.0;
#endif

    float wRock = smoothstep(rockSlope, rockSlope + 0.12, slope + (mid - 0.5) * 0.12);
    float wSand = 1.0 - smoothstep(waterY + beachWidth * 0.3, waterY + beachWidth + (mid - 0.5) * beachWidth, P.y);
    wSand *= 1.0 - wRock * 0.8;
    float wSnow = smoothstep(snowLine, snowLine + snowBlend, P.y + (mid - 0.5) * snowBlend - slope * snowBlend * 1.2);
    wSnow *= 1.0 - smoothstep(0.35, 0.7, slope);
    float wGrass2 = smoothstep(0.55, 0.75, macro) * 0.45;

    vec3 albedo, n;
    if (hasMaterials == 1) {
        vec2 uvG = P.xz * (0.16 * texScale);
        vec2 uvS = P.xz * (0.2 * texScale);
        vec3 aG, nG, a2, n2, aR, nR, aS, nS, aSn, nSn;
        if (customGrass == 1) { aG = texture(texture1, uvG).rgb; nG = N; }
        else sampleTop(0, uvG, N, detail, aG, nG);
        // Larger-scale second sample hides tiling in the distance
#if QUALITY >= 2
        vec3 aFar = texture(matAlbedo, vec3(uvG * 0.13 + 0.37, 0)).rgb;
        aG = mix(aG, aG * aFar * 2.2, 0.35 + 0.3 * smoothstep(20.0, 120.0, dist));
#endif
        albedo = aG; n = nG;
        if (QUALITY >= 1 && wGrass2 > 0.01) {
            sampleTop(1, uvG * 0.8, N, detail, a2, n2);
            albedo = mix(albedo, a2, wGrass2); n = normalize(mix(n, n2, wGrass2));
        }
        albedo *= grassTint;
        if (wSand > 0.01) {
            sampleTop(3, uvS, N, detail, aS, nS);
            albedo = mix(albedo, aS * sandTint, wSand); n = normalize(mix(n, nS, wSand));
        }
        if (wRock > 0.01) {
#if QUALITY >= 2
            sampleTriplanar(2, P * (0.09 * texScale), N, detail, aR, nR);
#else
            sampleTop(2, P.xz * (0.09 * texScale), N, detail, aR, nR);
#endif
            albedo = mix(albedo, aR * rockTint, wRock); n = normalize(mix(n, nR, wRock));
        }
        if (wSnow > 0.01) {
            sampleTop(4, uvS * 0.6, N, detail, aSn, nSn);
            albedo = mix(albedo, aSn * snowTint, wSnow); n = normalize(mix(n, nSn, wSnow));
        }
        // Village cobblestone streets: irregular worn edges where grass creeps between the stones
        float pave = villageMaskAt(P.xz).r;
        if (pave > 0.01) {
            float edge = noise4(P.xz * (1.0 / 6.0)).b;
            float wPave = smoothstep(0.25, 0.75, pave + (edge - 0.5) * 0.45);
            vec3 aP, nP;
            sampleTop(5, P.xz * (0.32 * texScale), N, detail, aP, nP);
            albedo = mix(albedo, aP * vec3(0.95, 0.92, 0.88), wPave);
            n = normalize(mix(n, nP, wPave));
        }
    } else {
        vec3 grass = (customGrass == 1 ? texture(texture1, P.xz * 0.2).rgb : vec3(0.30, 0.45, 0.18)) * grassTint;
        albedo = mix(grass, vec3(0.8, 0.72, 0.5) * sandTint, wSand);
        albedo = mix(albedo, vec3(0.42, 0.38, 0.35) * rockTint * (0.7 + 0.6 * mid), wRock);
        albedo = mix(albedo, vec3(0.95, 0.97, 1.0) * snowTint, wSnow);
        n = N;
    }
    albedo *= 0.92 + 0.16 * macro;
    albedo *= mix(0.6, 1.0, smoothstep(waterY - 0.3, waterY + 0.5, P.y));   // wet sand
    float depthBelow = waterY - P.y;
    if (depthBelow > 0.0) albedo = mix(albedo, albedo * waterShallow * 1.6, clamp(depthBelow / 8.0, 0.0, 0.8));

    // Lighting: sky ambient + sun (with terrain / tree shadows)
    float shadow = sunShadow(P);
    float canopy = hasCanopyShade == 1 ? texture(canopyShade, P.xz / (2.0 * canopyHalf) + 0.5).r : 0.0;
    float ao = 1.0 - 0.45 * canopy;
    vec3 amb = skyAmbient(n) * mix(vec3(0.55, 0.48, 0.40), vec3(1.0), n.y * 0.5 + 0.5) * 0.55 * ao;
    float ndl = dot(n, light);
    float diff = max(ndl, 0.0);
    float wrap = clamp((ndl + 0.25) / 1.25, 0.0, 1.0);
    float macroShade = mix(0.75, 1.0, clamp(dot(N, light) * 0.5 + 0.5, 0.0, 1.0));
    vec3 sun = lightColor * (0.6 * diff + 0.4 * wrap * wrap) * macroShade * shadow;
    vec3 lit = (amb + sun + pointLighting(P, n, numOutdoorLights)) * albedo;
    float spec = pow(max(dot(viewDir, reflect(-light, n)), 0.0), 32.0);
    lit += lightColor * spec * shadow * (0.25 * wSnow + 0.12 * wSand * (1.0 - smoothstep(waterY, waterY + 0.6, P.y)));

    if (QUALITY >= 2 && depthBelow > 0.0 && depthBelow < 25.0) {
        float c = caustics(P.xz, time) * exp(-depthBelow * 0.12) * max(dot(N, light), 0.0) * shadow;
        lit += lightColor * c * 1.3;
    }
    FragColor = vec4(applyFog(lit, P), 1.0);
}
