#version 330 core
// Quarter-resolution bright pass for bloom; alpha = bright sky near the sun (source of the sun rays)
in vec2 uv;
out vec4 outColor;
uniform sampler2D scene;
uniform sampler2D depth;
uniform vec2 uvScale;
uniform vec2 srcTexel;
uniform float threshold;
uniform vec2 sunUV;
uniform float aspect;
uniform int needSky;   // sun rays on screen: also build the sky mask

void main() {
    vec2 u = uv * uvScale;
    // 4 bilinear taps = 16 texels averaged (reduces flicker when downsampling 4x)
    vec3 c = vec3(0.0);
    float sky = 0.0;
    vec2 o[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    for (int i = 0; i < 4; ++i) {
        vec2 t = clamp(u + o[i] * srcTexel, vec2(0.0), uvScale - srcTexel);
        c += texture(scene, t).rgb;
        if (needSky == 1) sky += texture(depth, t).r >= 0.99999 ? 1.0 : 0.0;
    }
    c *= 0.25; sky *= 0.25;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = 0.2;
    float soft = clamp(l - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float w = max(soft, l - threshold) / max(l, 1e-4);
    vec2 d = (uv - sunUV) * vec2(aspect, 1.0);
    float nearSun = exp(-dot(d, d) * 6.0);
    outColor = vec4(c * w, sky * smoothstep(0.35, 0.9, l) * (0.35 + 0.65 * nearSun));
}
