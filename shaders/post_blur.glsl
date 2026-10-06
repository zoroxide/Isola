#version 330 core
// 9-tap Gaussian (5 bilinear fetches) along `step`
in vec2 uv;
out vec4 outColor;
uniform sampler2D src;
uniform vec2 step;
uniform vec2 texel;
uniform vec2 uvScale;
void main() {
    vec2 u = uv * uvScale;
    vec2 lim = uvScale - texel;
    vec4 c = texture(src, u) * 0.2270270270;
    c += texture(src, clamp(u + step * 1.3846153846, vec2(0.0), lim)) * 0.3162162162;
    c += texture(src, clamp(u - step * 1.3846153846, vec2(0.0), lim)) * 0.3162162162;
    c += texture(src, clamp(u + step * 3.2307692308, vec2(0.0), lim)) * 0.0702702703;
    c += texture(src, clamp(u - step * 3.2307692308, vec2(0.0), lim)) * 0.0702702703;
    outColor = c;
}
