#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 WaveNormal;
in float Crest;
in float Depth;

#include "common.glsl"

uniform float waterOpacity;
uniform vec3 waterShallow;
uniform vec3 waterDeep;
uniform float choppiness;
uniform sampler2D waterDetail;   // rg: ripple normal, b: foam noise, a: height

vec3 skyOrDefault(vec3 d, float lod) {
    if (hasSky == 1) return skyColor(d, lod);
    return mix(fogColor, vec3(0.25, 0.45, 0.80), smoothstep(0.0, 0.6, d.y));
}

void main() {
    vec3 light = normalize(-lightDir);
    vec3 viewVec = viewPos - FragPos;
    float dist = length(viewVec);
    vec3 v = viewVec / dist;
    vec2 p = FragPos.xz;

    // Fine ripples from two scrolling layers of the ripple texture (fade out with distance)
    vec4 r1 = texture(waterDetail, p * (1.0 / 14.0) + time * vec2(0.020, 0.011));
#if QUALITY >= 1
    vec4 r2 = texture(waterDetail, p * (1.0 / 5.3) - time * vec2(0.013, 0.027));
#else
    vec4 r2 = r1.gbar;
#endif
    vec2 d = ((r1.rg - 0.5) * 1.1 + (r2.rg - 0.5) * 0.7) * (1.0 - smoothstep(40.0, 350.0, dist));
    vec3 n = normalize(WaveNormal + vec3(d.x, 0.0, d.y));
    float choppyFoam = 0.6 * smoothstep(0.2, 1.0, choppiness);
    float shore = clamp(Depth, 0.0, 100.0);
    float shadow = sunShadow(FragPos);

    if (underwater == 1) {
        // Seen from below: Snell's window shows the sky, outside it the surface mirrors the deep
        vec3 nb = -n;
        float cosT = dot(v, nb);
        float window = smoothstep(0.62, 0.72, cosT);
        vec3 refr = refract(-v, nb, 1.33);
        vec3 through = skyOrDefault(dot(refr, refr) > 0.0 ? normalize(refr) : vec3(0.0, 1.0, 0.0), 2.0) * 1.1;
        vec3 col = mix(uwColor * 0.8, through, window);
        col += lightColor * pow(max(dot(-v, light), 0.0), 40.0) * window * shadow;
        FragColor = vec4(mix(col, uwColor, 1.0 - exp(-dist * 0.035)), 0.97);
        return;
    }

    float F = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 refl = skyOrDefault(reflect(-v, n), 1.0);
    vec3 body = mix(waterShallow, waterDeep, smoothstep(0.0, 9.0, shore));
    float diff = max(dot(n, light), 0.0);
    body *= 0.45 + (0.55 * diff * length(lightColor) * 0.6 + 0.3) * mix(0.6, 1.0, shadow);
    float sss = pow(max(dot(v, -light), 0.0), 3.0) * max(Crest, 0.0);
    body += waterShallow * lightColor * sss * 0.6 * shadow;

    vec3 col = mix(body, refl, F);
    vec3 h = normalize(light + v);
    col += lightColor * pow(max(dot(n, h), 0.0), 600.0) * 1.6 * shadow;

    // Foam: surf rolling onto the beach and whitecaps on the highest crests
    float foamNoise = r1.b * 0.6 + r2.b * 0.4;
    float surf = smoothstep(0.55, 0.95, sin(shore * 5.0 - time * 1.6 + foamNoise * 3.0) * 0.5 + 0.5);
    float edgeFoam = 1.0 - smoothstep(0.0, 0.25 + 0.2 * foamNoise, shore);
    float shoreFoam = max(edgeFoam, surf * (1.0 - smoothstep(0.2, 1.6, shore)) * 0.8) * smoothstep(0.25, 0.6, foamNoise);
    float crestFoam = smoothstep(0.72, 1.0, Crest) * smoothstep(0.55, 0.8, r2.b) * choppyFoam;
    float foam = clamp(shoreFoam + crestFoam, 0.0, 1.0);
    col = mix(col, vec3(0.95) * (0.6 + 0.4 * lightColor * mix(0.5, 1.0, shadow)), foam * 0.9);

    float alpha = clamp(waterOpacity * smoothstep(0.0, 0.6, shore) + F * 0.5 + foam * 0.6, 0.0, 1.0);
    alpha = max(alpha, smoothstep(6.0, 12.0, shore));   // deep water is opaque (the sea floor there isn't drawn)
    float fogF = fogAmount(FragPos);
    col = mix(col, fogBaseColor(-v), fogF);
    alpha = mix(alpha, 1.0, fogF);
    FragColor = vec4(col, alpha);
}
