#version 330 core
// Sun rays: radial blur of the bright sky (alpha of the bright pass) towards the sun
in vec2 uv;
out vec4 outColor;
uniform sampler2D src;
uniform vec2 uvScale;
uniform vec2 sunUV;
void main() {
    const int N = 40;
    vec2 delta = (uv - sunUV) / float(N) * 0.9;
    vec2 p = uv;
    float decay = 1.0, sum = 0.0;
    float jitter = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
    p -= delta * jitter;
    for (int i = 0; i < N; ++i) {
        p -= delta;
        vec2 q = clamp(p, vec2(0.0), vec2(1.0)) * uvScale;
        sum += texture(src, q).a * decay;
        decay *= 0.965;
    }
    outColor = vec4(vec3(sum / float(N) * 2.2), 1.0);
}
