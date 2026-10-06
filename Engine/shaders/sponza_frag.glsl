#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec4 Tangent;
in vec2 UV;

#include "common.glsl"

// glTF metal/roughness material (unit 0 base colour, 1 normal map, 5 roughness/metallic in r/g)
uniform sampler2D albedoTex;
uniform sampler2D normalTex;
uniform sampler2D metalRoughTex;
uniform vec4 baseColorFactor;
uniform float metallicFactor;
uniform float roughnessFactor;
uniform int hasNormalTex;
uniform int hasMetalRough;
uniform int alphaMask;
uniform float alphaCutoff;

// Baked light volume (see Sponza::bake): L1 spherical harmonics stored as (b.xyz, a),
// irradiance towards n = max(a + dot(b, n), 0); 1 = an open sky / full sunlight
uniform sampler3D skyVis;
uniform sampler3D bounceR;
uniform sampler3D bounceG;
uniform sampler3D bounceB;
uniform int hasVolume;
uniform vec3 volMin;
uniform vec3 volCell;
uniform vec3 volRes;
uniform float indirectStrength;

float shEval(vec4 c, vec3 n) { return max(c.w + dot(c.xyz, n), 0.0); }

void main() {
    vec4 albedo = texture(albedoTex, UV) * baseColorFactor;
    if (alphaMask == 1 && albedo.a < alphaCutoff) discard;
    vec3 base = albedo.rgb;

    vec3 light = normalize(-lightDir);
    vec3 v = normalize(viewPos - FragPos);
    vec3 ng = normalize(Normal);
    if (!gl_FrontFacing) ng = -ng;
    vec3 n = ng;
#if QUALITY >= 1
    if (hasNormalTex == 1) {
        vec2 xy = texture(normalTex, UV).rg * 2.0 - 1.0;
        vec3 tn = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
        vec3 T = normalize(Tangent.xyz - ng * dot(Tangent.xyz, ng));
        vec3 B = cross(ng, T) * Tangent.w;
        n = normalize(T * tn.x + B * tn.y + ng * tn.z);
    }
#endif
    vec2 mr = hasMetalRough == 1 ? texture(metalRoughTex, UV).rg : vec2(1.0, 0.0);
    float rough = clamp(mr.x * roughnessFactor, 0.05, 1.0);
    float metal = clamp(mr.y * metallicFactor, 0.0, 1.0);
    vec3 diffuseColor = base * (1.0 - metal);
    vec3 F0 = mix(vec3(0.04), base, metal);

    // --- Indirect: sky seen through the courtyard + sunlight bounced off the lit stone ---
    vec3 r = reflect(-v, n);
    float sky = 1.0, skyR = 1.0;
    vec3 bounce = vec3(0.0), bounceRefl = vec3(0.0);
    if (hasVolume == 1) {
        // Sampled a little off the surface so the cell behind the wall doesn't darken it
        vec3 uvw = ((FragPos + ng * 0.3 - volMin) / volCell) / volRes;
        vec4 s = texture(skyVis, uvw);
        vec4 br = texture(bounceR, uvw), bg = texture(bounceG, uvw), bb = texture(bounceB, uvw);
        // Floor: light through gaps narrower than the volume's cells (and further sky bounces)
        sky = max(shEval(s, n), 0.012);
        bounce = vec3(shEval(br, n), shEval(bg, n), shEval(bb, n));
#if QUALITY >= 2
        skyR = shEval(s, r);
        bounceRefl = vec3(shEval(br, r), shEval(bg, r), shEval(bb, r));
#else
        skyR = sky;
        bounceRefl = bounce;
#endif
    }
    vec3 ambient = skyAmbient(n) * sky * 0.75 + lightColor * bounce * indirectStrength;

    // --- Sun ---
    float shadow = sunShadow(FragPos + ng * 0.03);
    float ndl = max(dot(n, light), 0.0);
    vec3 h = normalize(light + v);
    float ndh = max(dot(n, h), 0.0), ndv = max(dot(n, v), 1e-3);
    float vdh = max(dot(v, h), 0.0);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - vdh, 5.0);
#if QUALITY >= 2
    // GGX / Smith-Schlick, the glTF reference BRDF
    float a = rough * rough, a2 = a * a;
    float dd = ndh * ndh * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159 * dd * dd);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = ndl / (ndl * (1.0 - k) + k) * ndv / (ndv * (1.0 - k) + k);
    vec3 spec = D * G * F / max(4.0 * ndl * ndv, 1e-3);
#else
    float shine = mix(256.0, 4.0, rough);
    vec3 spec = F * pow(ndh, shine) * (shine + 8.0) / 25.0;
#endif
    vec3 direct = (diffuseColor * (1.0 - F) + spec * 3.14159 * 0.25) * lightColor * ndl * shadow;

    // Environment reflection: the sky where it is visible, bounce light elsewhere
    vec3 Fr = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - ndv, 5.0);
    vec3 envSky = hasSky == 1 ? skyColor(r, rough * skyMaxLod) : skyAmbient(r);
    vec3 env = (envSky * skyR * 0.75 + lightColor * bounceRefl * indirectStrength) * Fr * (1.0 - rough * 0.7);

    vec3 col = direct + diffuseColor * ambient + env + pointLighting(FragPos, n, numPointLights) * diffuseColor;
    FragColor = vec4(applyFog(col, FragPos), 1.0);
}
