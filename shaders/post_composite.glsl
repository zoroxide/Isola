#version 330 core
// Final image: upscale with FXAA + adaptive sharpen (sharing the same 5 taps) + bloom + sun rays
// + colour grading + vignette
in vec2 uv;
out vec4 outColor;
uniform sampler2D scene;
uniform sampler2D bloom;
uniform sampler2D rays;
uniform vec2 uvScale;       // part of the scene texture that holds this frame
uniform vec2 sceneTexel;    // 1 / texture size
uniform int fxaa;
uniform float sharpen;
uniform float bloomIntensity;
uniform int useRays;
uniform vec3 rayColor;
uniform float vignette;
uniform float exposure;
uniform int rolloff;        // soft shoulder for highlights (used with eye adaptation)
uniform float contrast;
uniform float saturation;
uniform float warmth;
uniform float wobble;
uniform float time;

vec3 S(vec2 u) { return textureLod(scene, min(u, uvScale - sceneTexel * 0.5), 0.0).rgb; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 suv = uv;
    if (wobble > 0.0) suv += vec2(sin(uv.y * 18.0 + time * 1.7), cos(uv.x * 14.0 + time * 1.3)) * 0.0035;
    vec2 u = suv * uvScale;
    vec2 r = sceneTexel;
    vec3 M = S(u);
    vec3 c = M;
    if (fxaa == 1 || sharpen > 0.0) {
        vec3 NW = S(u + vec2(-r.x, -r.y)), NE = S(u + vec2(r.x, -r.y));
        vec3 SW = S(u + vec2(-r.x, r.y)), SE = S(u + vec2(r.x, r.y));
        float lNW = luma(NW), lNE = luma(NE), lSW = luma(SW), lSE = luma(SE), lM = luma(M);
        float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
        float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
        bool edge = fxaa == 1 && (lMax - lMin) > max(0.0312, lMax * 0.125);
        if (edge) {
            // FXAA: blur along the edge direction (compact version of Timothy Lottes' algorithm)
            vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
            float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
            dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * r;
            vec3 a = 0.5 * (S(u - dir * (1.0 / 6.0)) + S(u + dir * (1.0 / 6.0)));
            vec3 b = a * 0.5 + 0.25 * (S(u - dir * 0.5) + S(u + dir * 0.5));
            float lB = luma(b);
            c = (lB < lMin || lB > lMax) ? a : b;
        } else if (sharpen > 0.0) {
            // Contrast-adaptive sharpening restores detail lost when rendering below native resolution
            vec3 mn = min(M, min(min(NW, NE), min(SW, SE))), mx = max(M, max(max(NW, NE), max(SW, SE)));
            vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
            vec3 w = -amp * mix(0.1, 0.18, sharpen);
            c = clamp((M + (NW + NE + SW + SE) * w) / (1.0 + 4.0 * w), 0.0, 1.5);
        }
    }

    c += texture(bloom, u).rgb * bloomIntensity;
    if (useRays == 1) c += texture(rays, u).rgb * rayColor;

    // Grading
    c *= exposure * vec3(1.0 + warmth, 1.0, 1.0 - warmth);
    if (rolloff == 1) {
        const float k = 0.65;   // values above k approach 1 smoothly instead of clipping
        c = mix(c, k + (1.0 - k) * (1.0 - exp(-(c - k) / (1.0 - k))), step(vec3(k), c));
    }
    c = mix(vec3(luma(c)), c, saturation);
    c = (c - 0.5) * contrast + 0.5;
    vec2 d = uv - 0.5;
    c *= 1.0 - vignette * smoothstep(0.25, 0.85, dot(d, d) * 2.2);
    // Dither (interleaved gradient noise) hides banding in sky gradients
    c += (fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5) / 255.0;
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
